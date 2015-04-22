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

static void *run(hashpipe_thread_args_t * args)
{
// 	fprintf(stderr, "catcher's ibuf address: %p\n", (void *) &(*(args->ibuf)));
	gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->ibuf;
	hashpipe_status_t st = args->st;
	const char * status_key = args->thread_desc->skey;

	int rv;
	int block_idx = 0;
	int mcnt = 0;

    int scanning = 0;
    int scan_elapsed_time = 0;
    int scan_length = 0;

    hashpipe_status_lock_safe(&st);
    // Force SCANST to 0 to make sure we wait for user input
    hputi4(st.buf, "SCANST", 0);
    // Set default SCANLEN
    hputi4(st.buf, "SCANLEN", 5);
    hashpipe_status_unlock_safe(&st);

	while (run_threads())
	{
		hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for the current block to be filled
		while ((rv=gpu_output_databuf_wait_filled(db, block_idx)) != HASHPIPE_OK)
        {
            if (rv==HASHPIPE_TIMEOUT) {
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
		hputs(st.buf, status_key, "receiving");
        hashpipe_status_unlock_safe(&st);

        // If we are not in a scan...
        if (!scanning)
        {
            hashpipe_status_lock_safe(&st);
            // ... check to see if we should start a scan
            hgeti4(st.buf, "SCANST", &scanning);

            if (scanning)
            {
                // Now that we are in a scan, set the start prompt back to 0
                // Doing this here insures that we don't have
                //   redundant writes to status shared mem
                hputi4(st.buf, "SCANST", 0);
            }

            hashpipe_status_unlock_safe(&st);
            fprintf(stderr, "SCANST = %d\n", scanning);
//             sleep(1);
        }

        // Regardless of scan state, read from shared memory
		mcnt = db->block[block_idx].mcnt;
		fprintf(stderr, "\nReading from block %d on mcnt %d\n", block_idx, mcnt);


        // If we are in a scan...
        if (scanning)
        {
            fprintf(stderr, "Scanning. Elapsed time: %d\n", scan_elapsed_time);
            hashpipe_status_lock_safe(&st);

            // ...find out how long we should scan
            hgeti4(st.buf, "SCANLEN", &scan_length);
            hashpipe_status_unlock_safe(&st);

            // Check to see if we have a valid scan length from the user
            if (scan_length <= 0)
            {
                // If we don't, error...
                hashpipe_error(__FUNCTION__, "SCANLEN has either not been set or has been set to an invalid value");
                // ...stop the scan...
                scanning = 0;
                // ...and skip the rest of the block
                // TODO: should this be happening?
                continue;
            }

            // If we have scanned for the designated amount of time...
            if (scan_elapsed_time >= scan_length)
            {
                // ...write to disk
                fprintf(stderr, "This is where we would be writing FITS files to disk\n");
                scan_elapsed_time = 0;
                scanning = 0;
                //      int i;
                //      for (i = 0; i < NUM_ANTENNAS; i++) {
                //          fprintf(stderr, "\tdb->block[%d].data[%d]: %d\n", block_idx, i, db->block[block_idx].data[i]);
                //      }
            }
            scan_elapsed_time++;
        }

		// Mark block as full
        gpu_output_databuf_set_free(db, block_idx);

        // Setup for next block
		block_idx = (block_idx + 1) % NUM_BLOCKS;
// 		fprintf(stderr, "catcher's block_idx is now: %d\n", block_idx);

//      Will exit if thread has been cancelled
        pthread_testcancel();
	}

	return NULL;
}

static hashpipe_thread_desc_t fits_writer_thread = {
    name: "fits_writer_thread",
    skey: "TESTSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {gpu_output_databuf_create},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fits_writer_thread);
}