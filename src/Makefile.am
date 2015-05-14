# Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Correspondence concerning GBT software should be addressed as follows:
# GBT Operations
# National Radio Astronomy Observatory
# P. O. Box 2
# Green Bank, WV 24944-0002 USA

AM_CPPFLAGS     = -I"@HASHPIPE_INCDIR@" -I "/home/gbt1/cfitsio"

# AM_CFLAGS is used for all C compiles
AM_CFLAGS = -ggdb -fPIC -O3 -Wall  -fno-strict-aliasing
# -mavx <-- evil. won't let me write floats to shm
# -Werror <-- treats warnings as errors :(

# Convenience variables to group source files
gpu_output_databuf = gpu_output_databuf.h \
             gpu_output_databuf.c

fake_gpu = fake_gpu_thread.c \
           fits_writer_thread.c

# This is the paper_gpu plugin itself
lib_LTLIBRARIES        = fake_gpu.la
fake_gpu_la_SOURCES    = $(fake_gpu) $(gpu_output_databuf) fifo.c
fake_gpu_la_LIBADD    = -lrt -lcfitsio
fake_gpu_la_LDFLAGS     = -avoid-version -module -shared -export-dynamic
fake_gpu_la_LDFLAGS     += -L"@HASHPIPE_LIBDIR@" -Wl,-rpath,"@HASHPIPE_LIBDIR@"

# Installed scripts
dist_bin_SCRIPTS = ../../scripts/dmjd.py \
		   ../../scripts/run_scan \
		   ../../scripts/clean_ipc \
		   ../../scripts/clean_ipc.py \
		   ../../scripts/clean_sim \
		   ../../scripts/set_scan \
		   ../../scripts/set_status
