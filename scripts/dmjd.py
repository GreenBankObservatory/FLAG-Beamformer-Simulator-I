#!/usr/bin/env python
# -*- coding: iso-8859-1 -*-

# EXAMPLES:
# $ run_scan.sh --startin 5 --scanlength 5
# hashpipe_check_status -k STRTDMJD -d 57155.588552
# hashpipe_check_status -k SCANLEN -i 5
# [...]

# $ run_scan.sh --starttime 15:05:12:15:07:00 --scanlength 5
# hashpipe_check_status -k STRTDMJD -d 57154.629861
# hashpipe_check_status -k SCANLEN -i 5
# [...]

import sys, time, calendar, math
from subprocess import call
import shlex

def get_utc_str(time_utc):
    return "%s:%s:%s:%s:%s:%s" % (time_utc.tm_year, time_utc.tm_mon, time_utc.tm_mday, time_utc.tm_hour, time_utc.tm_min, time_utc.tm_sec)


def secs_2_dmjd(secs):
    dmjd = (secs/86400) + 40587
    return dmjd + ((secs % 86400)/86400.)

def dmjd_2_secs(dmjd):
    d, mjd = math.modf(dmjd)
    return (86400 * (mjd - 40587)) + (86400 * d)

def usage():
    print "Usage: %s [ --starttime ] [ --startin ] [--scanlength] " % "python dmjd.py"
    

if (len(sys.argv) <= 1):
    usage()

curr_time_sec = -1
curr_time_dmjd = -1
start_time_sec = -1
start_time_dmjd = -1

iter_argv = iter(sys.argv[1:])
for arg in iter_argv:
    if arg == "--starttime":
        requested_start_time = next(iter_argv)
        # TODO: Error checking?
        time_utc = time.strptime(requested_start_time, "%y:%m:%d:%H:%M:%S")
        time_secs = calendar.timegm(time_utc)
        start_time_dmjd = secs_2_dmjd(time_secs)

        print time_secs
        print secs_2_dmjd(time_secs)
        print dmjd_2_secs(secs_2_dmjd(time_secs))

        cmd = "hashpipe_check_status -k STRTDMJD -d %f" % start_time_dmjd
        print cmd
        call(shlex.split(cmd))
        
    elif arg == "--scanlength":
        scanlen = next(iter_argv)
        cmd = "hashpipe_check_status -k SCANLEN -i %s" % scanlen
        call(shlex.split(cmd))
        print cmd
        
    elif arg == "--startin":
        curr_time_sec = int(time.time())
        start_time_sec = curr_time_sec + int(next(iter_argv))
        curr_time_dmjd = secs_2_dmjd(curr_time_sec)
        start_time_dmjd = secs_2_dmjd(start_time_sec)
        
        #print "Seconds:         Current time is %f           Scan will start at %f." % (curr_time_sec, start_time_sec)
        #print "DMJD:            Current time is %f         Scan will start at %f." % (secs_2_dmjd(curr_time_sec), start_time_dmjd)
        #print "DMJD -> Secs:    Current time is %f           Scan will start at %f." % (dmjd_2_secs(curr_time_dmjd), dmjd_2_secs(start_time_dmjd))
        #print "UTC:             Current time is",
        #print get_utc_str(time.gmtime(curr_time_sec)),
        #print "   Scan will start at ",
        #print get_utc_str(time.gmtime(dmjd_2_secs(start_time_dmjd)))

        cmd = "hashpipe_check_status -k STRTDMJD -d %f" % start_time_dmjd
        print cmd

        call(shlex.split(cmd))
    else:
        print "Invalid arguments: %s" % (' '.join(sys.argv[1:]))
        usage()
        break

print "\nChecking status keys:"
cmd = "hashpipe_check_status -v"
print cmd
call(shlex.split(cmd))

#print "\nChecking start time:"

