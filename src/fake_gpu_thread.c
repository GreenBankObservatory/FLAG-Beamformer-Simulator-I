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

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(hashpipe_thread_args_t * args)
{
    gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    int rv;
    int block_idx = 0;
    int mcnt = 0;

    srand(time(NULL));

	open_fifo("/tmp/fake_gpu_control");

	int cmd = INVALID;

    while (run_threads())
    {
		fprintf(stderr, "# ");
		
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

		// TODO: spin until we receive a START from the user
		while (cmd == INVALID)
		{
			cmd = check_cmd();
// 			sleep(1);
// 			fprintf(stderr, "cmd: %d\n", cmd);
		}

// 		fprintf(stderr, "received command\n");

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

        sleep(1);

        hashpipe_status_lock_safe(&st);
        // Set status to sending
        hputs(st.buf, status_key, "sending");
        hashpipe_status_unlock_safe(&st);

        db->block[block_idx].header.mcnt = mcnt++;

        fprintf(stderr, "\nWriting to block %d on mcnt %d\n", block_idx, mcnt);

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
//         fprintf(stderr, "pitcher's block_idx is now: %d\n", block_idx);

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