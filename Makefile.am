ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS     =

AM_CPPFLAGS += -I"@HASHPIPE_INCDIR@"
# AM_CPPFLAGS += -I"@XGPU_INCDIR@"

# AM_CFLAGS is used for all C compiles
AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror -fno-strict-aliasing -mavx

# Convenience variables to group source files
gpu_output_databuf = src/gpu_output_databuf.h \
	        src/gpu_output_databuf.c

fake_gpu = src/fake_gpu_thread.c
# fake_gpu_test_thread.c

# This is the paper_gpu plugin itself
lib_LTLIBRARIES = fake_gpu.la
fake_gpu_la_SOURCES = $(fake_gpu) $(gpu_output_databuf)
fake_gpu_la_LIBADD   = -lrt
fake_gpu_la_LDFLAGS  = -avoid-version -module -shared -export-dynamic
# example_la_LDFLAGS += -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"
fake_gpu_la_LDFLAGS += -L"@HASHPIPE_LIBDIR@" -Wl,-rpath,"@HASHPIPE_LIBDIR@"

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
