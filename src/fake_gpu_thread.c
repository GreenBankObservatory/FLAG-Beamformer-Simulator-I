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
#include "time_stuff.h"
//#include "matrix_map.h"

#define SCAN_STATUS_LENGTH 10

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define MJD_1970_EPOCH (40587)

// #define DEBUG



int gpu_fifo_id;

// int old_to_new_map[GPU_BIN_SIZE];

static int init(struct hashpipe_thread_args *args)
{
    srand(time(NULL));

    char *user = getenv("USER");
    char fifo_loc[128];
    sprintf(fifo_loc, "/tmp/gpu_fifo_%s_%d", user, args->instance_id);
    fprintf(stderr, "Using fake_gpu control FIFO: %s\n", fifo_loc);

    gpu_fifo_id = open_fifo(fifo_loc);

    fprintf(stderr, "FAKE GPU HAS FD %d\n", gpu_fifo_id);
    if (gpu_fifo_id < 0)
        return -1;

    fprintf(stderr, "Data Size Stats:\n");
    fprintf(stderr, "\tNumber of channels:                           %10d channels\n", NUM_CHANNELS);
    fprintf(stderr, "\tBin size:                                     %10d elements\n", GPU_BIN_SIZE);
    fprintf(stderr, "\tElement size:                                 %10lu bytes\n", 2 * sizeof (float));
    fprintf(stderr, "\tNumber of elements in a block:                %10d elements\n", NUM_CHANNELS * GPU_BIN_SIZE);
    fprintf(stderr, "\tBlock size: (num_chans * bin_size * el_size): %10lu bytes\n",
            NUM_CHANNELS * GPU_BIN_SIZE * (2 * sizeof (float)));



    hashpipe_status_t st = args->st;

    hashpipe_status_lock_safe(&st);
    // Force SCANINIT to 0 to make sure we wait for user input
    hputi4(st.buf, "SCANINIT", 0);
    // Set default SCANLEN
    hputi4(st.buf, "SCANLEN", 2);
    // Set the scan to off by default
    hputs(st.buf, "SCANSTAT", "off");
    // Initialize start time to impossible value
    hputr8(st.buf, "STRTDMJD", -1.0);
    hashpipe_status_unlock_safe(&st);

    // get_mapping_C(GPU_BIN_SIZE, old_to_new_map);

    // fprintf(stderr, "Mapping: \n");

    // int i;
    // for (i = 0; i < GPU_BIN_SIZE; ++i)
    // {
    //     fprintf(stderr, "%d ", old_to_new_map[i]);
    // }

    // fprintf(stderr, "\n");

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

    timespec scan_start_time, scan_stop_time;
    timespec sleep_until;
#ifdef DEBUG
    timespec loop_start, loop_end;

    timespec shm_start, shm_stop;
    timespec blocked_start, blocked_stop;
    timespec tmp_start, tmp_stop;
    int64_t total_scan_ns = 0;
    int64_t scan_loop_ns = 0;
#endif

    int cmd = INVALID;

    double curr_time_dmjd = -1;
    double start_time_dmjd = -1;

    while (run_threads())
    {
#ifdef DEBUG
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);

        // Check for a command from the user
        cmd = check_cmd(gpu_fifo_id);
        // fprintf(stderr, "fake_gpu fd: %d cmd: %d\n", gpu_fifo_id, cmd);
        // sleep(1);
        if (cmd == START)
        {
            fprintf(stderr, "fake_gpu_thread received START!\n");

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
                (NUM_CHANNELS * GPU_BIN_SIZE * (2 * sizeof (float))) / INT_TIME,
                ((NUM_CHANNELS * GPU_BIN_SIZE * (2 * sizeof (float))) / INT_TIME) / (1024 * 1024 * 1024) * 8);
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
            hashpipe_status_lock_safe(&st);
            // Set status to sending
            hputs(st.buf, status_key, "writing");
            hashpipe_status_unlock_safe(&st);

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
            memset(db->block[block_idx].data, 0, NUM_CHANNELS * GPU_BIN_SIZE * 2);

            #define DIM 20

            // I am so sorry in advance; I really just don't have time to remove
            //   the horrendous code duplication here
            // This is the element index. It tracks the index of the current
            //   complex pair
            int elem_i = 0;
            // This is the 'channel' loop
            int i;
            for (i = 0; i < NUM_CHANNELS; i++)
            {
                // This is the 'column' loop
                int j;
                for (j = 0; j < DIM; j++)
                {
                    // This is the 'row' loop
                    // Together the 'column' and 'row' loops track our position
                    //   (as an ordered pair) in the matrix
                    int k;
                    for (k = 0; k < j+1; k++)
                    {
                        int l;
                        for (l = 0; l < 4; l++)
                        {
                            // We derive a 'real index' from the element counter.
                            // This is the index of the real half of the complex pair
                            int real_i = elem_i * 2;
                            // This is the index of the imaginary half of the complex pair
                            int imag_i = real_i + 1;

                            // We will derive the 'column' portion of the coordinate
                            //   and write it to the real half of the pair
                            // We start with an offset in order to create a smooth
                            //   ramp between the different blocks
                            int real_coord = DIM * 2 * block_idx;
                            // We will derive the 'row' portion of the coordinate
                            //   and write it to the imaginary half of the pair
                            // This allows us to write a ramp of ordered pairs to FITS
                            int imag_coord = real_coord;
                            // Account for the four diffent elements within
                            //   each block
                            if (l == 0)
                            {
                                real_coord += 2 * j;
                                imag_coord += 2 * k;
                            }
                            else if (l == 1)
                            {
                                real_coord += 2 * j;
                                imag_coord += 2 * k + 1;
                            }
                            else if (l == 2)
                            {
                                real_coord += 2 * j + 1;
                                imag_coord += 2 * k;
                            }
                            else if (l == 3)
                            {
                                real_coord += 2 * j + 1;
                                imag_coord += 2 * k + 1;
                            }
                            
                            // Now we simply write the data itself to the proper block
                            db->block[block_idx].data[real_i] = real_coord;
                            db->block[block_idx].data[imag_i] = imag_coord;
                            elem_i++;
                        }
                    }
                }
            }

#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &shm_stop);

//             // Calculate time taken to write to shm
            fprintf(stderr, "Time from shm_start to shm_stop is: %ld ns\n", ELAPSED_NS(shm_start, shm_stop));

            scan_loop_ns += ELAPSED_NS(shm_start, shm_stop);
#endif

            // Mark block as full
            gpu_output_databuf_set_filled(db, block_idx);

            // Setup for next block
            block_idx = (block_idx + 1) % NUM_BLOCKS;
            block_counter++;

#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &loop_end);
            fprintf(stderr, "Time from shm_stop to loop_end is: %ld ns\n",
                    ELAPSED_NS(shm_stop, loop_end));
            scan_loop_ns += ELAPSED_NS(shm_stop, loop_end);
#endif

            sleep_until.tv_nsec += INT_TIME_NS;
            // Handle overflow of ns->s
            if(sleep_until.tv_nsec >= 1000000000)
            {
                sleep_until.tv_nsec -= 1000000000;
                sleep_until.tv_sec++;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleep_until, NULL);

            // TODO: For debugging, print out how long we are waiting this cycle

#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &tmp_start);
            fprintf(stderr, "Time from loop_end to tmp_start: %ld\n",
                    ELAPSED_NS(loop_end, tmp_start));
            scan_loop_ns += ELAPSED_NS(loop_end, tmp_start);
#endif

            // Test to see if we are done scanning
            if (block_counter >= num_blocks_to_write)
            {
                if (block_counter > num_blocks_to_write)
                {
                    fprintf(stderr,
                            "ERROR: block_counter is greater than num_blocks_to_write, \
                            which really shouldn't be possible. Exiting...\n");
                    exit(EXIT_FAILURE);
                }

                hputs(st.buf, "SCANSTAT", "off");
                clock_gettime(CLOCK_MONOTONIC, &scan_stop_time);
                fprintf(stderr, "\nScan complete!\n\tRequested scan time: %d\n\tActual scan time: %f\n",
                        requested_scan_length, (double)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0);
                fprintf(stderr, "\nWe wrote %d blocks to shared memory\n", block_counter);

                fprintf(stderr, "\nPACKET_RATE: %d\nINT_TIME: %f\nN: %d\n",
                    PACKET_RATE, INT_TIME, N);
#ifdef DEBUG
                fprintf(stderr, "\tScan time via intervals was %ld ns = %f sec\n",
                        total_scan_ns, (double)total_scan_ns / 1000000000.0);
#endif
                if ((double)(ELAPSED_NS(scan_start_time, scan_stop_time) - requested_scan_length * 1000000000) > 0)
                {
                    fprintf(stderr, "\tThis scan was %f%% slower than it should have been.\n",
                            (((double)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0) / (double)requested_scan_length) * 100 - 100);
                    fprintf(stderr, "\tThis means that each block was written, on average, %.0f ns too slowly.\n",
                            (double)(ELAPSED_NS(scan_start_time, scan_stop_time) - requested_scan_length * 1000000000) / (double)num_blocks_to_write);
                }
                else
                {
                    fprintf(stderr, "\tThis scan was %f%% faster than it should have been.\n",
                            (((double)ELAPSED_NS(scan_start_time, scan_stop_time) / 1000000000.0) / (double)requested_scan_length) * 100 - 100);
                    fprintf(stderr, "\tThis means that each block was written, on average, %.0f ns too quickly.\n",
                            (double)(ELAPSED_NS(scan_start_time, scan_stop_time) - requested_scan_length * 1000000000) / (double)num_blocks_to_write);
                }

                fprintf(stderr, "\nEND OF SCAN\n");

                block_counter = 0;
                mcnt = 0;
            }


#ifdef DEBUG
            clock_gettime(CLOCK_MONOTONIC, &tmp_stop);
            scan_loop_ns += ELAPSED_NS(tmp_start, tmp_stop);
            fprintf(stderr, "Time from tmp_start to tmp_stop is: %ld\n",
                    ELAPSED_NS(tmp_start, tmp_stop));
            total_scan_ns += scan_loop_ns;
            scan_loop_ns = 0;
            fprintf(stderr, "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
#endif
        }

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    return THREAD_OK;
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
