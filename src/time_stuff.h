#ifndef _time_stuff_h
#define _time_stuff_h

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include "hashpipe.h"
#include <sys/time.h>

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define MJD_1970_EPOCH (40587)



double timeval_2_mjd(struct timeval *tv);
time_t dmjd_2_secs(double dmjd);
double get_curr_time_dmjd();

#endif