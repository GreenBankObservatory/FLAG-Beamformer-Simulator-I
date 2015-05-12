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

#include "hashpipe.h"
#include "gpu_output_databuf.h"
#include "fitsio.h"
#include "fifo.h"

#define SCAN_STATUS_LENGTH 10

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define MJD_1970_EPOCH (40587)

void nsleep(long ns);
double timeval_2_mjd(timeval *tv);
time_t dmjd_2_secs(double dmjd);
double get_curr_time_dmjd();

static int init(struct hashpipe_thread_args *args)
{
    srand(time(NULL));
    if (open_fifo("/tmp/fake_gpu_control") == -1)
        return -1;
    
    hashpipe_status_t st = args->st;

    hashpipe_status_lock_safe(&st);
    // Force SCANINIT to 0 to make sure we wait for user input
    hputi4(st.buf, "SCANINIT", 0);
    // Set default SCANLEN
    hputi4(st.buf, "SCANLEN", 2);
    // Set the scan to off by default
    hputs(st.buf, "SCANSTAT", "off");
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
    int current_block = 0;
    // packets per second received from roach
    const int PACKET_RATE = 100;
    // Integration time in ms?? can't remember
    const float INT_TIME = .5;

    // This is what mcnt will increment by
    const int N = (int)(PACKET_RATE * INT_TIME);
    const float GPU_DELAY = (float)N / (float)PACKET_RATE;

    timespec loop_start, loop_end;
    timespec scan_start_time, scan_stop_time;

    int cmd = INVALID;
    
    timeval curr_timeval;
//     timeval start_timeval;

    double curr_time_dmjd = -1;
    double start_time_dmjd = -1;

    while (run_threads())
    {
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
        
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);

        // spin until we receive a START from the user
        cmd = check_cmd();
        if (cmd == START)
        {
            fprintf(stderr, "START received!\n");
            
            hashpipe_status_lock_safe(&st);
            hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
            hashpipe_status_unlock_safe(&st);
            if (strcmp(scan_status, "scanning") == 0)
            {
                fprintf(stderr, "We are already in a scan\n");
                continue;
            }

            // Calculate when the scan should start
            // TODO: right now this just starts 5 seconds after we recv START
            // Set curr_timeval to the current time
            gettimeofday(&curr_timeval, 0);
            // Set start_timeval to 5 seconds after that
//             start_timeval = curr_timeval;
//             start_timeval.tv_sec += 5;

//             curr_time_dmjd = timeval_2_mjd(&curr_timeval);
//             start_time_dmjd = timeval_2_mjd(&start_timeval);

//             // TODO: This is just for dev purposes; in production this value will be set elsewhere
//             hashpipe_status_lock_safe(&st);
//             // TODO: Verify that my changes to hputr8 were necessary/did not break anything
//             hputr8(st.buf, "STRTDMJD", start_time_dmjd);
//             hashpipe_status_unlock_safe(&st);
    
            hashpipe_status_lock_safe(&st);
            // ...find out how long we should scan
            hgeti4(st.buf, "SCANLEN", &requested_scan_length);
            
            if (!hgetr8(st.buf, "STRTDMJD", &start_time_dmjd))
            {
                fprintf(stderr, "STRTDMJD keyword is not set!\n");
                continue;
            }
                
            hputs(st.buf, "SCANSTAT", "committed");
            hashpipe_status_unlock_safe(&st);

//             fprintf(stderr, "SRTDMJD: %f\n", start_time_dmjd
            
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

            // TODO: check that num blocks to write is an integer
        }
        else if (cmd == STOP || cmd == QUIT)
        {
            fprintf(stderr, "Stop observations.\n");

            hashpipe_status_lock_safe(&st);
            hputs(st.buf, "SCANSTAT", "off");
            hashpipe_status_unlock_safe(&st);

            current_block = 0;
            mcnt = 0;

            if (cmd == QUIT)
            {
                fprintf(stderr, "Quitting.\n");
                // TODO: Why doesn't this work?
                pthread_exit(NULL);
                break;
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
            }
        }
        // If we are "scanning"...
        else if (strcmp(scan_status, "scanning") == 0)
        {
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

            hashpipe_status_lock_safe(&st);
            // Set status to sending
            hputs(st.buf, status_key, "writing");
            hashpipe_status_unlock_safe(&st);

            db->block[block_idx].header.mcnt = mcnt;
            mcnt += N;

            fprintf(stderr, "\nWriting to block %d on mcnt %d\n", block_idx, db->block[block_idx].header.mcnt);

            // Write data to shared memory
            int i;
            for (i = 0; i < NUM_CHANNELS; i++)
            {
                int j;
                for (j = 0; j < BIN_SIZE * 2; j += 2)
                {
                    // index counters
                    int real_i = j + (i * BIN_SIZE * 2);
                    int imag_i = j + (i * BIN_SIZE * 2) + 1;
                    // real half of pair
                    db->block[block_idx].data[real_i] = j/2 + (block_idx * BIN_SIZE);
                    // imaginary half of pair
                    db->block[block_idx].data[imag_i] = j/2 + .5 + (block_idx * BIN_SIZE);
    //                 fprintf(stderr, "wrote real to %d: %f\n", real_i, db->block[block_idx].data[real_i]);
    //                 fprintf(stderr, "wrote imag to %d: %f\n", imag_i, db->block[block_idx].data[imag_i]);
                }
            }

            // Mark block as full
            gpu_output_databuf_set_filled(db, block_idx);

            // Setup for next block
            block_idx = (block_idx + 1) % NUM_BLOCKS;
            current_block++;
            fprintf(stderr, "\tCurrent block is: %d\n", current_block);

            clock_gettime(CLOCK_MONOTONIC, &loop_end);

            // delay in ns
            float delay = GPU_DELAY * 1000000000 - ELAPSED_NS(loop_start, loop_end);
            fprintf(stderr, "\tWaiting %f seconds\n", delay / 1000000000);
            if (delay <= 0)
            {
                fprintf(stderr, "WARNING: A negative delay indicates that the scan is NOT running in real time\n");
            }
            nsleep(delay);

            // Test to see if we are done scanning
            if (current_block >= num_blocks_to_write)
            {
                current_block = 0;
                mcnt = 0;
                hputs(st.buf, "SCANSTAT", "off");
                clock_gettime(CLOCK_MONOTONIC, &scan_stop_time);
                fprintf(stderr, "\nScan complete!\n\tRequested scan time: %d\n\tActual scan time: %f\n",
                        requested_scan_length, (float)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0);

                fprintf(stderr, "\nPACKET_RATE: %d\nINT_TIME: %f\nN: %d\nGPU_DELAY: %f\n",
                    PACKET_RATE, INT_TIME, N, GPU_DELAY);

                fprintf(stderr, "\nEND OF SCAN\n");
            }
        }

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }
    
    return THREAD_OK;
}

void nsleep(long ns)
{
    timespec delay;

    delay.tv_sec = ns / 1000000000;
    delay.tv_nsec = ns % 1000000000;

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