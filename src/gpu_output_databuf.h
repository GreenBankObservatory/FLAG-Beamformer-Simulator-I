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

#ifndef _gpu_output_databuf_h
#define _gpu_output_databuf_h

#include <stdint.h>
#include "hashpipe_databuf.h"
// #include "config.h"
// #define CACHE_ALIGNMENT 128
#define NUM_ANTENNAS 40
// The bin size is the number of elements in the lower trianglular
//   portion of the covariance matrix
//   (41 * 20) gives us the number of complex pair elements
#define BIN_SIZE 2112
// #define BIN_SIZE (41 * 20)
// #define BIN_SIZE 4
// This is the number of frequency channels that we will be correlating
//   It will be either 5, 50, or 160, and probably should always be a macro
//   For the purposes of this simulator we don't care about the input to the correlator
//   except that the number of input channels will indicate the number of output channels
//   That is, the total number of complex pairs we will be writing to shared memory
//   is given as: BIN_SIZE * NUM_CHANNELS
#define NUM_CHANNELS 160
#define TOTAL_DATA_SIZE (BIN_SIZE * NUM_CHANNELS * 2)

#define NUM_BLOCKS 2
// #define SCANLEN 5

// TODO: Cache alignment???

typedef struct gpu_output_databuf_block_header {
	int mcnt;
	// time
} gpu_output_databuf_block_header_t;

typedef struct gpu_output_databuf_block {
	gpu_output_databuf_block_header_t header;
	// we must double the elements since CFITSIO interperates every two elements as a pair
	float data[TOTAL_DATA_SIZE];
} gpu_output_databuf_block_t;

typedef struct gpu_output_databuf {
	hashpipe_databuf_t header;
	gpu_output_databuf_block_t block[NUM_BLOCKS];
} gpu_output_databuf_t;

typedef struct timeval timeval;
typedef struct timespec timespec;
/*
 * OUTPUT BUFFER FUNCTIONS
 */

hashpipe_databuf_t *gpu_output_databuf_create(int instance_id, int databuf_id);

static inline void gpu_output_databuf_clear(gpu_output_databuf_t *d)
{
    hashpipe_databuf_clear((hashpipe_databuf_t *)d);
}

static inline gpu_output_databuf_t *gpu_output_databuf_attach(int instance_id, int databuf_id)
{
    return (gpu_output_databuf_t *)hashpipe_databuf_attach(instance_id, databuf_id);
}

static inline int gpu_output_databuf_detach(gpu_output_databuf_t *d)
{
    return hashpipe_databuf_detach((hashpipe_databuf_t *)d);
}

static inline int gpu_output_databuf_block_status(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_block_status((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_total_status(gpu_output_databuf_t *d)
{
    return hashpipe_databuf_total_status((hashpipe_databuf_t *)d);
}

static inline int gpu_output_databuf_wait_free(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_busywait_free(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_wait_filled(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_busywait_filled(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_set_free(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_free((hashpipe_databuf_t *)d, block_id);
}

static inline int gpu_output_databuf_set_filled(gpu_output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_filled((hashpipe_databuf_t *)d, block_id);
}

#endif // _PAPER_DATABUF_H
