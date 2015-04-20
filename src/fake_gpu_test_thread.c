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

	while (run_threads())
	{
		hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

		while ((rv=gpu_output_databuf_wait_filled(db, block_idx)) != HASHPIPE_OK) {
              if (rv==HASHPIPE_TIMEOUT) {
                  hashpipe_status_lock_safe(&st);
                  hputs(st.buf, status_key, "blocked");
                  hashpipe_status_unlock_safe(&st);
                  continue;
              } else {
                  hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                  pthread_exit(NULL);
                  break;
              }
          }

		hashpipe_status_lock_safe(&st);
		hputs(st.buf, status_key, "receiving");
// 		hputi4(st.buf, "NETBKOUT", block_idx);
		hashpipe_status_unlock_safe(&st);


// 		sleep(1);

		mcnt = db->block[block_idx].mcnt;
		fprintf(stderr, "\nReading from block %d on mcnt %d\n", block_idx, mcnt);

		int i;
		for (i = 0; i < NUM_ANTENNAS; i++) {
			fprintf(stderr, "\tdb->block[%d].data[%d]: %d\n", block_idx, i, db->block[block_idx].data[i]);
		}

		// Mark block as full
        gpu_output_databuf_set_free(db, block_idx);

        // Setup for next block
		block_idx = (block_idx + 1) % NUM_BLOCKS;
// 		fprintf(stderr, "catcher's block_idx is now: %d\n", block_idx);


        /* Will exit if thread has been cancelled */
        pthread_testcancel();
	}

	return NULL;
}

static hashpipe_thread_desc_t fake_gpu_test_thread = {
    name: "fake_gpu_test_thread",
    skey: "TESTSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {gpu_output_databuf_create},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fake_gpu_test_thread);
}