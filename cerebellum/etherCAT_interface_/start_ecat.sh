#!/bin/bash
# start_ecat.sh

cd "$(dirname "$0")"
exec sudo env LD_LIBRARY_PATH=/opt/etherlab/lib \
    taskset -c 3 chrt -f 90 ./l7ec_pp_loop_shared_dc