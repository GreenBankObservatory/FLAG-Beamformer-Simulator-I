#include "time_stuff.h"

double timeval_2_mjd(struct timeval *tv)
{
    double dmjd = tv->tv_sec / 86400 + MJD_1970_EPOCH;

    dmjd += (tv->tv_sec % 86400) / 86400.0;

    return dmjd;
}

// Converts a DMJD to a time_t (seconds since epoch)
time_t dmjd_2_secs(double dmjd)
{
    double d;
    double mjd;

    d = modf(dmjd, &mjd);

    return (86400 * (mjd - MJD_1970_EPOCH)) + (86400 * d);
}

// Gets the current time as a DMJD
double get_curr_time_dmjd()
{
   struct timeval time;
   gettimeofday(&time, 0);
   return timeval_2_mjd(&time);
}