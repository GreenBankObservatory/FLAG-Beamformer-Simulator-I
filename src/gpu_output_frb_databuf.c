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

/* gpu_output_databuf.c
 *
 * Routines for creating and accessing main data transfer
 * buffer in shared memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>

#include "gpu_output_frb_databuf.h"

hashpipe_databuf_t *gpu_output_frb_databuf_create(int instance_id, int databuf_id)
{
// 	fprintf(stderr, "Creating an gpu_output_databuf with instance_id: %d and databuf_id: %d\n",
// 			instance_id, databuf_id);
	
    /* Calc databuf sizes */
    size_t header_size = sizeof (hashpipe_databuf_t);
    size_t block_size  = sizeof (gpu_output_databuf_block_t);
    fprintf(stderr, "buffer size is: %lu\n", NUM_BLOCKS * sizeof (gpu_output_databuf_block_t));
    int    n_block = NUM_BLOCKS;

    return hashpipe_databuf_create(
        instance_id, databuf_id, header_size, block_size, n_block);
}
