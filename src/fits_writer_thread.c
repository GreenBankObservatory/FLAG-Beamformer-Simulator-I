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
// For printing uint64_t
#include <inttypes.h>

#include "fitsio.h"
#include "fifo.h"
#include "hashpipe.h"
#include "gpu_output_databuf.h"

#define SCAN_STATUS_LENGTH 10

// Forward declarations for the sake of prettiness
int fits_write_row(fitsfile *fptr, gpu_output_databuf_block_t *block, int row_num);
fitsfile *create_fits_file(char *filename, int scan_duration, int scan_num, int *st);

int fits_fifo_id;

static int init(struct hashpipe_thread_args *args)
{
    char *fifo_loc = "/tmp/tchamber/fits_writer_control";
    fits_fifo_id = open_fifo(fifo_loc);
    fprintf(stderr, "FITS WRITER HAS FD %d\n", fits_fifo_id);
    if (fits_fifo_id < 0)
        return -1;

    fprintf(stderr, "Using fits_writer_thread control FIFO: %s\n", fifo_loc);

    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{
	gpu_output_databuf_t *db = (gpu_output_databuf_t *)args->ibuf;
	hashpipe_status_t st = args->st;
	const char * status_key = args->thread_desc->skey;

	int rv;
	int block_idx = 0;

    int cmd = INVALID;

    struct timespec start, stop;
    // Elapsed time in ns
    uint64_t scan_elapsed_time = 0;
    // Requested scan length in seconds
    int requested_scan_length = 0;

	int num_blocks_to_write = 0;
    int block_counter = 0;

    // The current scanning status
    char scan_status[SCAN_STATUS_LENGTH];

    // FITS file shit
    int status = 0;
    int row_num = 0;
    int scan_num = 0;
    fitsfile *fptr = NULL;
    char filename[256];

    // hashpipe_status_lock_safe(&st);
    // // Force SCANINIT to 0 to make sure we wait for user input
    // hputi4(st.buf, "SCANINIT", 0);
    // // Set default SCANLEN
    // hputi4(st.buf, "SCANLEN", 5);
    // // Set the scan to off by default
    // hputs(st.buf, "SCANSTAT", "off");
    // hashpipe_status_unlock_safe(&st);

	while (run_threads())
	{

		// hashpipe_status_lock_safe(&st);
  //       hputs(st.buf, status_key, "waiting");
  //       hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
  //       hashpipe_status_unlock_safe(&st);

        
        // fprintf(stderr, "Looping\n");

        // TODO: Is this the right status to be setting?
		hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "receiving");
        hashpipe_status_unlock_safe(&st);

        cmd = check_cmd(fits_fifo_id);
        // sleep(1);
        // fprintf(stderr, "fits_writer_thread fd: %d, cmd: %d\n", fits_fifo_id, cmd);
        if (cmd == START)
        {
            fprintf(stderr, "fits_writer_thread received START!\n");

            hashpipe_status_lock_safe(&st);
            hgets(st.buf, "SCANSTAT", SCAN_STATUS_LENGTH, scan_status);
            hashpipe_status_unlock_safe(&st);

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
            hashpipe_status_unlock_safe(&st);

            // TODO: calculate number of blocks to write based on SCANLEN
            num_blocks_to_write = (PACKET_RATE * requested_scan_length) / N;

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

            // Create/open FITS file
            // TODO: Portable filenames
            sprintf(filename, "/tmp/tchamber/sim1fits/scan%d.fits", scan_num);
            fptr = create_fits_file(filename, requested_scan_length, scan_num, &status);
            if (status)
            {
                hashpipe_error(__FUNCTION__, "Error creating fits file");
                pthread_exit(NULL);
            }
            // Row number will return to 0 on each new scan
            row_num = 0;
            scan_num++;

            // Get the current time
            clock_gettime(CLOCK_MONOTONIC, &start);
            // fprintf(stderr, "Starting scan at time: %ld\n", start.tv_sec);
            fprintf(stderr, "FITS writer is ready to write\n");
        }

        if (strcmp(scan_status, "scanning") == 0)
        {
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

            fprintf(stderr, "Writing a row to FITS. Elapsed time: %ld\n", scan_elapsed_time);

            // write FITS data!
//             fprintf(stderr, "writing row of data\n");

            // mcnt = db->block[block_idx].header.mcnt;
            fits_write_row(fptr, &(db->block[block_idx]), row_num++);

            clock_gettime(CLOCK_MONOTONIC, &stop);
            scan_elapsed_time = ELAPSED_NS(start, stop);

            // If we have scanned for the designated amount of time...
            // This is converting ns to sec since the scan duration is specified in seconds
            // TODO: surely there is a better place to do this
            if (block_counter >= num_blocks_to_write)
            {
                // ...write to disk
                fprintf(stderr, "Closing FITS file\n");
                // write last data row, close FITS file
                fits_close_file(fptr, &status);
                if (status)          /* print any error messages */
                  fits_report_error(stderr, status);

                scan_elapsed_time = 0;
                // hputs(st.buf, "SCANSTAT", "off");
            }

            block_counter++;
        }

		// Mark block as free
        gpu_output_databuf_set_free(db, block_idx);

        // Setup for next block
		block_idx = (block_idx + 1) % NUM_BLOCKS;
// 		fprintf(stderr, "catcher's block_idx is now: %d\n", block_idx);

//      Will exit if thread has been cancelled
        pthread_testcancel();
	}

	return THREAD_OK;
}

static hashpipe_thread_desc_t fits_writer_thread = {
    name: "fits_writer_thread",
    skey: "TESTSTAT",
    init: init,
    run:  run,
    ibuf_desc: {gpu_output_databuf_create},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fits_writer_thread);
}

fitsfile *create_fits_file(char *filename, int scan_duration, int scan_num, int *st) {
    fprintf(stderr, "create_fits_file\n");
    fitsfile *fptr;
    int status = 0;

    char keyname[10];
    char comment[64];

    fits_create_file(&fptr, filename, &status);
    if (status)          /* print any error messages */
    {
      fits_report_error(stderr, status);
      return(fptr);
    }

    fits_open_file(&fptr, filename, READWRITE, &status);
    if (status)          /* print any error messages */
      fits_report_error(stderr, status);

    // Initialize primary header
    fits_create_img(fptr, 8, 0, 0, &status);
    if (status)          /* print any error messages */
      fits_report_error(stderr, status);

    strcpy(keyname, "SCANNUM");
    //strcpy(value, "myvalue");
    strcpy(comment, "scan number");

    fits_update_key_lng(fptr,
                        keyname,
                        scan_num,
                        comment,
                        &status);
    if (status)          /* print any error messages */
      fits_report_error(stderr, status);


    strcpy(keyname, "SCANDUR");
    strcpy(comment, "Duration of scan (seconds)");
    fits_update_key_lng(fptr,
                        keyname,
                        scan_duration,
                        comment,
                        &status);
    if (status)          /* print any error messages */
      fits_report_error(stderr, status);


    // Use this to allow variable bin sizes
    // TODO: Should this only be 3 chars long?
    char data_form[10];
    sprintf(data_form, "%dC", GPU_BIN_SIZE * NUM_CHANNELS);
    //debug
    fprintf(stderr, "data_form: %s\n", data_form);

    // write data table
    char ext_name[] = "DATA";
    int number_columns = 2;
    char *ttype_state[] =
        {"MCNT", "DATA"};
    char *tform_state[] =
        {"1J", data_form};
    char *tunit_state[] =
        {" ", " "};

    fits_create_tbl(fptr,
                    BINARY_TBL,
                    0,
                    number_columns,
                    ttype_state,
                    tform_state,
                    tunit_state,
                    ext_name,
                    &status);
    if (status)          /* print any error messages */
      fits_report_error(stderr, status);

    fprintf(stderr, "Created FITS file\n");
    *st = status;
    return(fptr);
}

// int mcnt, float *data
int fits_write_row(fitsfile *fptr, gpu_output_databuf_block_t *block, int row_num) {
    int status = 0;
	int *mcnt = &(block->header.mcnt);
	float *data = block->data;
    long data_elements = GPU_BIN_SIZE * NUM_CHANNELS;
	fprintf(stderr, "num data els: %ld\n", data_elements);

	fits_write_col_int(fptr, 1,row_num + 1, 1, 1, mcnt, &status);

	if (status)
      fits_report_error(stderr, status);

    fits_write_col_cmp(fptr, 2, row_num + 1, 1, data_elements, data, &status);

    if (status)
      fits_report_error(stderr, status);

    fprintf(stderr, "what\n");

    return(status);

}