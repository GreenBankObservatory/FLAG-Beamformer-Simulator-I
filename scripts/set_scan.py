# -*- coding: iso-8859-1 -*-

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

import commands
import subprocess
import shlex
import os.path
from time import sleep
import signal
import sys

def cleanup(signal, frame):
    print "> Killing fake_gpu and vegasFitsWriter..."
    hashpipe.terminate()

    with open(vegas_fifo, "a") as vegas_fifo_file:
        vegas_fifo_file.write("QUIT")

    exit(0)

signal.signal(signal.SIGINT, cleanup)

# Clear out FIFOs
print "Clearing control FIFOs"
hash_fifo = "/tmp/fake_gpu_control"
vegas_fifo = "/tmp/vegas_fits_control"
try:
    fifo = hash_fifo
    open(fifo, 'w+').close()
    fifo = vegas_fifo
    open(fifo, 'w+').close()
except IOError:
    print "Could not open the ", fifo, " FIFO"
    exit(0)

# Run hashpipe
print "> Starting the fake_gpu hashpipe plugin"
cmd = "taskset 0x0606 hashpipe -p fake_gpu -I 0 -o BINDHOST=px1-2.gb.nrao.edu -o GPUDEV=0 -o XID=0 -c 3 fake_gpu_thread"
hashpipe = subprocess.Popen(shlex.split(cmd))
#print "> cmd: ", cmd

# Run set_status
print "> Setting necessary status memory key values"
# This gets the directory that this script lives in and looks for set_status within it
cmd = "%s/set_status" %os.path.dirname(os.path.realpath(__file__))
#print "> cmd: ", cmd
subprocess.call(cmd)

# Run vegasFitsWriter
print "> Starting vegasFitsWriter"
vegasFitsWriter = subprocess.Popen("/home/sandboxes/tchamber/FLAG-Beamformer-Devel/src/dibas_fits_writer/vegasFitsWriter")

print "> READY TO START SCANNING!"
print "> Use the \"run_scan\" script to run a scan\n"

# This is just to make the output prettier (come out in order)
sleep(.1)

res = ""
while (res.lower() != "q" and res.lower() != "quit"):
    res = raw_input("\n> Enter \"Q[UIT]\" to quit!\n# ")

cleanup()
