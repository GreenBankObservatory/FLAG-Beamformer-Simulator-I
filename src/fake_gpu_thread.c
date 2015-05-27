//# Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
//#
//# This program is free software; you can redistribute it and/or modify
//# it under the terms of the GNU General Public License as published by
//# the Free Software Foundation; either version 2 of the License, or
//# (at your option) any later version.
//#
//# This program is distributed in the hope that it will be useful, but
//# WITHOUT ANY WARRANTY; without even the implied warranty of
//# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//# General Public License for more details.
//#
//# You should have received a copy of the GNU General Public License
//# along with this program; if not, write to the Free Software
//# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//#
//# Correspondence concerning GBT software should be addressed as follows:
//# GBT Operations
//# National Radio Astronomy Observatory
//# P. O. Box 2
//# Green Bank, WV 24944-0002 USA

// run with:
// $ clean_ipc; taskset 0x0606 hashpipe -p fake_gpu -I 0 -o BINDHOST=px1-2.gb.nrao.edu -o GPUDEV=0 -o XID=0 -c 3 fake_gpu_thread

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

#include "hashpipe.h"
#include "gpu_output_databuf.h"
#include "fitsio.h"
#include "fifo.h"

#define SCAN_STATUS_LENGTH 10

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define MJD_1970_EPOCH (40587)

#define DEBUG

void nsleep(long ns);
double timeval_2_mjd(timeval *tv);
time_t dmjd_2_secs(double dmjd);
double get_curr_time_dmjd();

static int init(struct hashpipe_thread_args *args)
{
    srand(time(NULL));

    char *fifo_loc = "/tmp/tchamber/fake_gpu_control";
    if (open_fifo(fifo_loc) == -1)
        return -1;

    fprintf(stderr, "Using fake_gpu control FIFO: %s\n", fifo_loc);
    fprintf(stderr, "Data Size Stats:\n");
    fprintf(stderr, "\tNumber of channels:                           %10d channels\n", NUM_CHANNELS);
    fprintf(stderr, "\tBin size:                                     %10d elements\n", BIN_SIZE);
    fprintf(stderr, "\tElement size:                                 %10lu bytes\n", 2 * sizeof (float));
    fprintf(stderr, "\tNumber of elements in a block:                %10d elements\n", NUM_CHANNELS * BIN_SIZE);
    fprintf(stderr, "\tBlock size: (num_chans * bin_size * el_size): %10lu bytes\n",
            NUM_CHANNELS * BIN_SIZE * (2 * sizeof (float)));



    hashpipe_status_t st = args->st;

    hashpipe_status_lock_safe(&st);
    // Force SCANINIT to 0 to make sure we wait for user input
    hputi4(st.buf, "SCANINIT", 0);
    // Set default SCANLEN
    hputi4(st.buf, "SCANLEN", 2);
    // Set the scan to off by default
    hputs(st.buf, "SCANSTAT", "off");
    hputr8(st.buf, "STRTDMJD", -1.0);
    hashpipe_status_unlock_safe(&st);

    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{
    gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    // Return value; temporary value used to evaluate result of function calls
    int rv;

    // The id of the current block
    int block_idx = 0;
    // The current frame counter value
    int mcnt = 0;

    // The current status of the scan
    char scan_status[SCAN_STATUS_LENGTH];
    // Requested scan length in seconds
    int requested_scan_length = -1;

    // The number of blocks we will write in a scan. Derived from requested_scan_length
    int num_blocks_to_write = -1;
    int block_counter = 0;
    // packets per second received from roach
    const int PACKET_RATE = 303750;
    // This is what mcnt will increment by
    const int N = 303;
    // Integration time in seconds
    // This is the amount of time that we will sleep for (total) at every block write
    const float INT_TIME = (float)N / (float)PACKET_RATE;
    const int64_t INT_TIME_NS = INT_TIME * 1000000000;

    timespec loop_start, loop_end;
    timespec scan_start_time, scan_stop_time;
    timespec sleep_until;
#ifdef DEBUG
    timespec shm_start, shm_stop;
    timespec blocked_start, blocked_stop;
    timespec tmp_start, tmp_stop;
    int64_t total_scan_ns = 0;
    int64_t scan_loop_ns = 0;
#endif

    int cmd = INVALID;

    double curr_time_dmjd = -1;
    double start_time_dmjd = -1;

    uint64_t scan_ns = 0;

    while (run_threads())
    {
        clock_gettime(CLOCK_MONOTONIC, &loop_start);

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);

        // Check for a command from the user
        cmd = check_cmd();
        if (cmd == START)
        {
            fprintf(stderr, "START received!\n");

            hashpipe_status_lock_safe(&st);
            hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
            hashpipe_status_unlock_safe(&st);
            // If we are either scanning or committed to a scan, continue with loop
            if (strcmp(scan_status, "scanning") == 0 || strcmp(scan_status, "committed") == 0)
            {
                if (strcmp(scan_status, "scanning") == 0)
                    fprintf(stderr, "We are already in a scan\n");
                if (strcmp(scan_status, "committed") == 0)
                    fprintf(stderr, "We are already committed to a scan\n");
                continue;
            }

            hashpipe_status_lock_safe(&st);
            // ...find out how long we should scan
            hgeti4(st.buf, "SCANLEN", &requested_scan_length);
            hgetr8(st.buf, "STRTDMJD", &start_time_dmjd);
            hputs(st.buf, "SCANSTAT", "committed");
            hashpipe_status_unlock_safe(&st);


            if (start_time_dmjd < 0)
            {
                fprintf(stderr, "STRTDMJD is set to a negative value!\n");
                continue; //why is this starting still?
            }

            // calculate number of blocks to write based on SCANLEN
            num_blocks_to_write = (PACKET_RATE * requested_scan_length) / N;
            fprintf(stderr, "Number of blocks to write: %d\n", num_blocks_to_write);

            fprintf(stderr, "The scan will start at DMJD: %f\n", start_time_dmjd);
            fprintf(stderr, "The scan will start in %lu seconds\n", dmjd_2_secs(start_time_dmjd) - dmjd_2_secs(get_curr_time_dmjd()));
            fprintf(stderr, "The scan will last %d seconds\n", requested_scan_length);

            // Check to see if the scan length is correct...
            if (requested_scan_length <= 0)
            {
                // ...if not, error...
                hashpipe_error(__FUNCTION__, "SCANLEN has either not been set or has been set to an invalid value");
                // ...stop the scan...
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, "SCANSTAT", "off");
                hashpipe_status_unlock_safe(&st);
                // ...and skip the rest of the block
                // TODO: should this be happening?
                continue;
            }

            fprintf(stderr, "Data Rate Stats:\n");
            fprintf(stderr, "\tIntegration Time:    %10f seconds\n", INT_TIME);
            fprintf(stderr, "\tRequired Data Rate:  %10f writes/second\n", 1/INT_TIME);
            fprintf(stderr, "\tPacket Rate:         %10d samples/second\n", PACKET_RATE);
            fprintf(stderr, "\tIntegration Size:    %10d samples\n", N);
            fprintf(stderr, "\tData Rate to Shared Memory:\n");
            fprintf(stderr, "\t\t%.0f bytes/second; %f Gb/s\n",
                (NUM_CHANNELS * BIN_SIZE * (2 * sizeof (float))) / INT_TIME,
                ((NUM_CHANNELS * BIN_SIZE * (2 * sizeof (float))) / INT_TIME) / (1024 * 1024 * 1024) * 8);
            fprintf(stderr, "\tData Rate to Disk:\n");
            fprintf(stderr, "\t\t%.0f bytes/second; %f Gb/s\n",
                (NUM_CHANNELS * 820 * (2 * sizeof (float))) / INT_TIME,
                ((NUM_CHANNELS * 820 * (2 * sizeof (float))) / INT_TIME) / (1024 * 1024 * 1024) * 8);

            // TODO: check that num blocks to write is an integer
        }
        else if (cmd == STOP || cmd == QUIT)
        {
            fprintf(stderr, "Stop observations.\n");

            hashpipe_status_lock_safe(&st);
            hputs(st.buf, "SCANSTAT", "off");
            hashpipe_status_unlock_safe(&st);

            block_counter = 0;
            mcnt = 0;

            if (cmd == QUIT)
            {
                fprintf(stderr, "Quitting.\n");
                // TODO: Why doesn't this work?
                pthread_exit(0);
//                 break;
//                 exit(EXIT_SUCCESS);
            }
        }

        // Now we can check if we are in a scan or not
        hashpipe_status_lock_safe(&st);
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);

        // If we are "committed" - that is, we are waiting to reach the scan start time...
        if (strcmp(scan_status, "committed") == 0)
        {
            curr_time_dmjd = get_curr_time_dmjd();
            if (curr_time_dmjd >= start_time_dmjd && start_time_dmjd != -1)
            {
                fprintf(stderr, "Starting scan!\n");
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, "SCANSTAT", "scanning");
                hashpipe_status_unlock_safe(&st);

                // Start the scan timer
                clock_gettime(CLOCK_MONOTONIC, &scan_start_time);
                // Mark the time that all sleep intervals will be based off of
                clock_gettime(CLOCK_MONOTONIC, &sleep_until);
            }
        }
        // If we are "scanning"...
        else if (strcmp(scan_status, "scanning") == 0)
        {
#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &blocked_start);
#endif
            // Wait for the current block to be set to free
            while ((rv=gpu_output_databuf_wait_free(db, block_idx)) != HASHPIPE_OK)
            {
                if (rv==HASHPIPE_TIMEOUT)
                {
                    hashpipe_status_lock_safe(&st);
                    hputs(st.buf, status_key, "blocked");
                    hashpipe_status_unlock_safe(&st);
                    continue;
                }
                else
                {
                    hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                    pthread_exit(NULL);
                    break;
                }
            }

#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &blocked_stop);
            fprintf(stderr, "Time from blocked_start to blocked_stop is: %ld\n", ELAPSED_NS(blocked_start, blocked_stop));
            scan_loop_ns += ELAPSED_NS(blocked_start, blocked_stop);
#endif
//             hashpipe_status_lock_safe(&st);
//             // Set status to sending
//             hputs(st.buf, status_key, "writing");
//             hashpipe_status_unlock_safe(&st);

            db->block[block_idx].header.mcnt = mcnt;
            mcnt += N;

#ifdef DEBUG
//             fprintf(stderr, "\tCurrent block is: %d\n", block_counter);
//             fprintf(stderr, "\tWriting to block %d on mcnt %d\n", block_idx, db->block[block_idx].header.mcnt);

            // Benchmark our write to shared memory
            clock_gettime(CLOCK_MONOTONIC, &shm_start);

            fprintf(stderr, "Time from blocked_stop to shm_start is: %ld ns\n",
                    ELAPSED_NS(blocked_stop, shm_start));
            scan_loop_ns += ELAPSED_NS(blocked_stop, shm_start);
#endif

            // Zero out our shm block's data
            memset(db->block[block_idx].data, 0, NUM_CHANNELS * BIN_SIZE * 2);

//             fprintf(stderr, "\tBIN_SIZE: %d\n", BIN_SIZE);
//             fprintf(stderr, "\tNONZERO_BIN_SIZE: %d\n", NONZERO_BIN_SIZE);

            // Write data to shared memory
            int i;
            for (i = 0; i < NUM_CHANNELS; i++)
            {
                int j;
                for (j = 0; j < NONZERO_BIN_SIZE * 2; j += 2)
                {
                    // index counters
                    int real_i = j + (i * NONZERO_BIN_SIZE * 2);
                    int imag_i = j + (i * NONZERO_BIN_SIZE * 2) + 1;
                    // real half of pair
                    db->block[block_idx].data[real_i] = j/2 + (block_idx * NONZERO_BIN_SIZE);
                    // imaginary half of pair
                    db->block[block_idx].data[imag_i] = j/2 + .5 + (block_idx * NONZERO_BIN_SIZE);
//                     fprintf(stderr, "wrote real to %d: %f\n", real_i, db->block[block_idx].data[real_i]);
//                     fprintf(stderr, "wrote imag to %d: %f\n", imag_i, db->block[block_idx].data[imag_i]);
                }
            }

#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &shm_stop);

//             // Calculate time taken to write to shm
//             fprintf(stderr, "-----\n");
//             scan_ns += ELAPSED_NS(shm_start, shm_stop);
//             double average_ns = scan_ns / block_counter;
            fprintf(stderr, "Time from shm_start to shm_stop is: %ld ns\n", ELAPSED_NS(shm_start, shm_stop));
            scan_loop_ns += ELAPSED_NS(shm_start, shm_stop);
//             fprintf(stderr, "The running average after %d writes is %f ns\n", block_counter, average_ns);
//             fprintf(stderr, "-----\n");
#endif

            // Mark block as full
            gpu_output_databuf_set_filled(db, block_idx);

            // Setup for next block
            block_idx = (block_idx + 1) % NUM_BLOCKS;
            block_counter++;



            clock_gettime(CLOCK_MONOTONIC, &loop_end);
#ifdef DEBUG
            fprintf(stderr, "Time from shm_stop to loop_end is: %ld ns\n", ELAPSED_NS(shm_stop, loop_end));
            scan_loop_ns += ELAPSED_NS(shm_stop, loop_end);
#endif

            // delay in ns
            int64_t loop_ns = ELAPSED_NS(loop_start, loop_end);
//             int64_t delay = INT_TIME * 1000000000 - loop_ns;

            sleep_until.tv_nsec += INT_TIME_NS;
            if(sleep_until.tv_nsec >= 1000000000)
            {
                sleep_until.tv_nsec -= 1000000000;
                sleep_until.tv_sec++;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleep_until, NULL);

            // TODO: Verify that this block is still valid after changing to clock_nanosleep
            int64_t delay = ELAPSED_NS(loop_end, sleep_until);
            if (delay <= 0)
            {
                fprintf(stderr, "WARNING: This write was %ld ns too slow\n", delay * -1);
            }
            else
            {
                #ifdef DEBUG
                fprintf(stderr, "\tThe loop has so far taken %lu ns, so we will remove this amount from our integration time:\n", loop_ns);
                fprintf(stderr, "\t\t%ld - %ld = %ld\n", (uint64_t)(INT_TIME * 1000000000), loop_ns, delay);
                fprintf(stderr, "\tWaiting %ld ns (%f seconds)\n", delay, (double)delay / 1000000000.0);
                #endif
//                 nsleep(delay);
            }

            clock_gettime(CLOCK_MONOTONIC, &tmp_start);
            scan_loop_ns += ELAPSED_NS(loop_end, tmp_start);
            fprintf(stderr, "Time from loop_end to tmp_start: %ld\n",
                    ELAPSED_NS(loop_end, tmp_start));



            // Test to see if we are done scanning
            if (block_counter >= num_blocks_to_write)
            {

                hputs(st.buf, "SCANSTAT", "off");
                clock_gettime(CLOCK_MONOTONIC, &scan_stop_time);
                fprintf(stderr, "\nScan complete!\n\tRequested scan time: %d\n\tActual scan time: %f\n",
                        requested_scan_length, (double)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0);
                fprintf(stderr, "\nWe wrote %d blocks to shared memory\n", block_counter);

                fprintf(stderr, "\nPACKET_RATE: %d\nINT_TIME: %f\nN: %d\n",
                    PACKET_RATE, INT_TIME, N);

                fprintf(stderr, "\tScan time via intervals was %ld ns = %f sec\n",
                        total_scan_ns, (double)total_scan_ns / 1000000000.0);

                fprintf(stderr, "\tThis scan was %f%% slower than it should have been.\n",
                        (((double)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0) / (double)requested_scan_length) * 100 - 100);
                fprintf(stderr, "\tThis means that each block was written, on average, %.0f ns too slowly.\n",
                        (double)(ELAPSED_NS(scan_start_time, scan_stop_time) - requested_scan_length * 1000000000) / (double)num_blocks_to_write);

                fprintf(stderr, "\nEND OF SCAN\n");

                block_counter = 0;
                mcnt = 0;
                scan_ns = 0;
            }



            clock_gettime(CLOCK_MONOTONIC, &tmp_stop);
            scan_loop_ns += ELAPSED_NS(tmp_start, tmp_stop);
            fprintf(stderr, "Time from tmp_start to tmp_stop is: %ld\n",
                    ELAPSED_NS(tmp_start, tmp_stop));

            total_scan_ns += scan_loop_ns;
            scan_loop_ns = 0;

            fprintf(stderr, "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
        }



        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    return THREAD_OK;
}

void nsleep(int64_t ns)
{
    timespec delay;

//     delay.tv_sec = ns / 1000000000;
//     delay.tv_nsec = ns % 1000000000;
    delay.tv_sec = 0;
    delay.tv_nsec = ns;

    nanosleep(&delay, NULL);
}

double timeval_2_mjd(timeval *tv)
{
    double dmjd = tv->tv_sec / 86400 + MJD_1970_EPOCH;

    dmjd += (tv->tv_sec % 86400) / 86400.0;

    return dmjd;
}

// Converts a DMJD to a time_t (seconds since epoch)
time_t dmjd_2_secs(double dmjd)
{
    double d;
    double mjd;

    d = modf(dmjd, &mjd);

    return (86400 * (mjd - MJD_1970_EPOCH)) + (86400 * d);
}

// Gets the current time as a DMJD
double get_curr_time_dmjd()
{
   timeval time;
   gettimeofday(&time, 0);
   return timeval_2_mjd(&time);
}

static hashpipe_thread_desc_t fake_gpu_thread = {
    name: "fake_gpu_thread",
    skey: "FGPUSTAT",
    init: init,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {gpu_output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fake_gpu_thread);
}