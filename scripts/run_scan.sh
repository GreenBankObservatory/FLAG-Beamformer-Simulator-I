wait()
{
    echo "> Waiting to continue"
    read
}

echo "> Cleaning up..."
/home/sandboxes/tchamber/paper/simulator_1/scripts/cleanup.sh


# Run the fake_gpu
echo "> Starting the fake_gpu hashpipe plugin"
taskset 0x0606 hashpipe -p fake_gpu -I 0 -o BINDHOST=px1-2.gb.nrao.edu -o GPUDEV=0 -o XID=0 -c 3 fake_gpu_thread &

# wait

# Set status memory keys
echo "> Setting status keys required for vegasFitsWriter"
/home/sandboxes/tchamber/paper/simulator_1/scripts/set_status.sh

# Runs dmjd script to set up start times; passes all our arguments to it
echo "> Setting start time related status keys"
python /home/sandboxes/tchamber/paper/simulator_1/scripts/dmjd.py "$@"

# wait

#Run the fits writer
echo "> Running fits writer"
/home/sandboxes/tchamber/vegas_devel3/vegas_devel/src/dibas_fits_writer/vegasFitsWriter &

# wait

# Start
echo "> Starting fake_gpu"
echo "START" > /tmp/fake_gpu_control

# wait

echo "> Starting vegas fits io"
echo "START" > /tmp/vegas_fits_control

echo "---------------PRESS ANY KEY TO QUIT---------------"
wait

pkill hashpipe
echo "> fake_gpu should have exited cleanly"

echo "QUIT" > /tmp/vegas_fits_control
echo "> vegasFitsWriter should have exited cleanly"

