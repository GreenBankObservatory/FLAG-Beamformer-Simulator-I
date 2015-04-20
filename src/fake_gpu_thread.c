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

static void *run(hashpipe_thread_args_t * args)
{
    gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    int rv;
    int block_idx = 0;
    int mcnt = 0;

    srand(time(NULL));

//     fits_open_file(NULL, "", 0, NULL);

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
//         hputi4(st.buf, "NETBKOUT", block_idx);
        hashpipe_status_unlock_safe(&st);

    
        sleep(1);
    
    
        db->block[block_idx].mcnt = mcnt++;

        fprintf(stderr, "\nWriting to block %d on mcnt %d\n", block_idx, mcnt);
    
        int i;
        for (i = 0; i < NUM_ANTENNAS; i++) {
            db->block[block_idx].data[i] = i + (block_idx * NUM_ANTENNAS);
//             db->block[block_idx].data[i] = rand(); 
        }

        // Mark block as full
        gpu_output_databuf_set_filled(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % NUM_BLOCKS;
//         fprintf(stderr, "pitcher's block_idx is now: %d\n", block_idx);

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }
    
    return NULL;
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