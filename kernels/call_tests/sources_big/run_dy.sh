#! /bin/sh
source /home/levinth/set11_1.sh
export LD_LIBRARY_PATH=./:$LD_LIBRARY_PATH

taskset 0x1 ./FOO_dynamic 100000
