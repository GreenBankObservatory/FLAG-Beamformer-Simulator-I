// run with:
// clean_ipc; taskset 0x0606 hashpipe -p example -I 0 -o BINDHOST=px1-2.gb.nrao.edu -o GPUDEV=0 -o XID=0 -c 3 fake_gpu_thread -c 2 catcher_thread


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
	gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->obuf;
	hashpipe_status_t st = args->st;
	const char * status_key = args->thread_desc->skey;

	int rv;
	int block_idx = 0;
	int counter = 0;
	
	while (run_threads())
	{
		hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

		while ((rv=gpu_output_databuf_wait_free(db, block_idx)) != HASHPIPE_OK) {
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
		hputs(st.buf, status_key, "sending");
// 		hputi4(st.buf, "NETBKOUT", block_idx);
		hashpipe_status_unlock_safe(&st);

		
		sleep(1);
		fprintf(stderr, "\nPitching!\n");
		
		db->block[block_idx].counter = counter++;
		db->block[block_idx].one   = 1 * (block_idx + 1);
		db->block[block_idx].two   = 2 * (block_idx + 1);
		db->block[block_idx].three = 3 * (block_idx + 1);
		db->block[block_idx].four  = 4 * (block_idx + 1);
		db->block[block_idx].five  = 5 * (block_idx + 1);

// 		fprintf(stderr, "Pitcher counter: %d\n", counter);

		int i;
		for (i = 0; i < 6; i++) {
			db->block[block_idx].six[i] = i * (block_idx + 1);
		}

		// Mark block as full
        gpu_output_databuf_set_filled(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % NUM_BLOCKS;
// 		fprintf(stderr, "pitcher's block_idx is now: %d\n", block_idx);

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
	}
	
	return NULL;
}

static hashpipe_thread_desc_t fake_gpu_thread = {
    name: "fake_gpu_thread",
    skey: "PITCSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {gpu_output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fake_gpu_thread);
}