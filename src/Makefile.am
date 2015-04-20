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

AM_CPPFLAGS     = -I"@HASHPIPE_INCDIR@" -I "/home/gbt1/cfitsio"
# AM_CPPFLAGS += -I"@XGPU_INCDIR@"

# AM_CFLAGS is used for all C compiles
AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror -fno-strict-aliasing -mavx

# Convenience variables to group source files
gpu_output_databuf = gpu_output_databuf.h \
                     gpu_output_databuf.c

fake_gpu = fake_gpu_thread.c \
           fake_gpu_test_thread.c

# This is the paper_gpu plugin itself
lib_LTLIBRARIES = fake_gpu.la
fake_gpu_la_SOURCES = $(fake_gpu) $(gpu_output_databuf)
fake_gpu_la_LIBADD   = -lrt -lcfitsio
fake_gpu_la_LDFLAGS  = -avoid-version -module -shared -export-dynamic
# example_la_LDFLAGS += -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"
fake_gpu_la_LDFLAGS += -L"@HASHPIPE_LIBDIR@" -Wl,-rpath,"@HASHPIPE_LIBDIR@"
#fake_gpu_la_LDFLAGS += -L/home/gbt1/cfitsio/x86-64_

# Installed scripts
# dist_bin_SCRIPTS = example_init.sh

# Non-installed test programs
# noinst_PROGRAMS = paper_fluff_test
# paper_fluff_test_SOURCES = paper_fluff_test.c $(paper_fluff)
# The paper_fluff_test_CFLAGS is an autotools workaround.
# http://www.gnu.org/software/automake/manual/html_node/Objects-created-both-with-libtool-and-without.html
# paper_fluff_test_CFLAGS = $(AM_CFLAGS)
# paper_fluff_test_LDADD = -lrt

# vi: set ts=8 noet :
