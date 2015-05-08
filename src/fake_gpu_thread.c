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
// $ clean_ipc; taskset 0x0606 hashpipe -p fake_gpu -I 0 -o BINDHOST=px1-2.gb.nrao.edu -o GPUDEV=0 -o XID=0 -c 3 fake_gpu_thread -c 2 fake_gpu_test_thread

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

void nsleep(long ns)
{
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = ns;

//     fprintf(stderr, "sleeping for %ld nanoseconds\n", delay.tv_nsec);
    nanosleep(&delay, NULL);
}

static void *run(hashpipe_thread_args_t * args)
{
    gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    int rv;
    int block_idx = 0;
    int mcnt = 0;

    srand(time(NULL));

    // The current scanning status
    char scan_status[SCAN_STATUS_LENGTH];
    // Requested scan length in seconds
    int requested_scan_length = 0;

    float num_blocks_to_write = 0.0f;
    int current_block = 0;
    // packets per second received from roach
    const int PACKET_RATE = 1000;
    // .1ms = .0001 sec
    const float INT_TIME = .0001;

    // Will be 30.3 but truncated to 30
//     const int N = PACKET_RATE * INT_TIME;
    const int N = 4;
    const float GPU_DELAY = (float)N / (float)PACKET_RATE;

    struct timespec start, stop;
    struct timespec scan_start, scan_stop;
    long elapsed_time;

    open_fifo("/tmp/fake_gpu_control");

    int cmd = INVALID;

    // TODO: Maybe put some of this in the init function?
    hashpipe_status_lock_safe(&st);
    // Force SCANINIT to 0 to make sure we wait for user input
    hputi4(st.buf, "SCANINIT", 0);
    // Set default SCANLEN
    hputi4(st.buf, "SCANLEN", 5);
    // Set the scan to off by default
    hputs(st.buf, "SCANSTAT", "off");
    hashpipe_status_unlock_safe(&st);

    while (run_threads())
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);

        // spin until we receive a START from the user
        cmd = check_cmd();

        if (cmd == START)
        {
            clock_gettime(CLOCK_MONOTONIC, &scan_start);
            
            hashpipe_status_lock_safe(&st);
            hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
            hashpipe_status_unlock_safe(&st);
    
            if (strcmp(scan_status, "scanning") == 0)
            {
                fprintf(stderr, "We are already in a scan\n");
                continue;
            }
    
            hashpipe_status_lock_safe(&st);
            // ...set status to scanning...
            hputs(st.buf, "SCANSTAT", "scanning");
            // ...find out how long we should scan
            hgeti4(st.buf, "SCANLEN", &requested_scan_length);
            hashpipe_status_unlock_safe(&st);

            // TODO: calculate number of blocks to write based on SCANLEN
            num_blocks_to_write = (PACKET_RATE * requested_scan_length) / N;
            fprintf(stderr, "num blocks to write: %f\n", num_blocks_to_write);

            // Check to see if the scan length is correct...
            if (requested_scan_length <= 0)
            {
                // ...if not, error...
                hashpipe_error(__FUNCTION__, "SCANLEN has either not been set or has been set to an invalid value");
                // ...stop the scan...
                hputs(st.buf, "SCANSTAT", "off");
                // ...and skip the rest of the block
                // TODO: should this be happening?
                continue;
            }

            // TODO: check that num blocks to write is an integer
        }


        // Now we can check if we are in a scan or not
        hashpipe_status_lock_safe(&st);
        hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
        hashpipe_status_unlock_safe(&st);
        if (strcmp(scan_status, "scanning") == 0)
        {
//             clock_gettime(CLOCK_MONOTONIC, &start);
            
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
            hputs(st.buf, status_key, "sending");
            hashpipe_status_unlock_safe(&st);

            mcnt += N;
            db->block[block_idx].header.mcnt = mcnt;

            fprintf(stderr, "\nWriting to block %d on mcnt %d\n", block_idx, db->block[block_idx].header.mcnt);

            // Write data to shared memory
            int i;
            for (i = 0; i < NUM_CHANNELS; i++)
            {
                int j;

                for (j = 0; j < BIN_SIZE * 2; j += 2)
                {
                    int real_i = j + (i * BIN_SIZE * 2);
                    int imag_i = j + (i * BIN_SIZE * 2) + 1;
                    // real
                    db->block[block_idx].data[real_i] = j/2 + (block_idx * BIN_SIZE);
                    // imaginary
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
            fprintf(stderr, "\tcurrent block is: %d\n", current_block);

            clock_gettime(CLOCK_MONOTONIC, &stop);
            elapsed_time = ELAPSED_NS(start, stop);

            float delay = GPU_DELAY * 1000000000 - elapsed_time ;
            fprintf(stderr, "\tWaiting %f seconds\n", delay / 1000000000);
            if (delay <= 0)
            {
                fprintf(stderr, "WARNING: A negative delay indicates that the scan is NOT running in real time\n");
//                 exit(EXIT_FAILURE);
            }
            nsleep(delay);

            // Test to see if we are done scanning
            if (current_block >= num_blocks_to_write)
            {
                
                current_block = 0;
                hputs(st.buf, "SCANSTAT", "off");
                clock_gettime(CLOCK_MONOTONIC, &scan_stop);
                fprintf(stderr, "Scan complete. \n\tRequested scan time: %d\n\tActual scan time: %f\n",
                        requested_scan_length, (float)ELAPSED_NS(scan_start, scan_stop) / 1000000000.0);
            }
        }

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }
    
    return THREAD_OK;
}

static hashpipe_thread_desc_t fake_gpu_thread = {
    name: "fake_gpu_thread",
    skey: "FGPUSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {gpu_output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fake_gpu_thread);
}