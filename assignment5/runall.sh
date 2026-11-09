#!/bin/bash


if [ $# -lt 1 ]
then
        echo "Usage : ./runall.sh 1|2|3|4|5|additional|clean|submit"
        exit
fi

case "$1" in

1)  echo "---         Setting up Part 1         ---"
    (cd partAdditional && make unload)
    rmmod -f SW
    rmmod -f KEY
    cd part1 && make clean && make
    echo "---  Running Part 1 (Press <Enter> to quit)  ---"
    echo "Running in 3..."
    sleep 1
    echo "2..."
    sleep 1
    echo "1..."
    sleep 1
    exec ./part1
    ;;
2)  echo "---         Setting up Part 2         ---"
    (cd partAdditional && make unload)
    rmmod -f SW
    rmmod -f KEY
    cd part2 && make clean && make
    echo "---  Running Part 2 (Press <Enter> to quit)  ---"
    echo "Running in 3..."
    sleep 1
    echo "2..."
    sleep 1
    echo "1..."
    sleep 1
    exec ./part2
    ;;
3)  echo "---         Setting up Part 3         ---"
    (cd partAdditional && make unload)
    rmmod -f SW
    rmmod -f KEY
    cd part3 && make clean && make
    echo "---  Running Part 3 (Press Ctrl-C to quit)  ---"
    echo "Running in 3..."
    sleep 1
    echo "2..."
    sleep 1
    echo "1..."
    sleep 1
    exec ./part3
    ;;
4)  echo "---         Setting up Part 4         ---"
    (cd partAdditional && make unload)
    rmmod -f SW
    rmmod -f KEY
    cd part4 && make clean && make
    echo "---  Running Part 4 (Ctrl-C to quit)  ---"
    echo "Running in 3..."
    sleep 1
    echo "2..."
    sleep 1
    echo "1..."
    sleep 1
    exec ./part4
    ;;
5)  echo "---        Setting up Part 5          ---"
    (cd partAdditional && make unload)
    rmmod KEY_SW 2>/dev/null || true
    rmmod KEY 2>/dev/null || true
    rmmod SW 2>/dev/null || true

    if [ ! -f drivers/KEY_SW.ko ]; then
        echo "ERROR: drivers/KEY_SW.ko was not found."
        echo "Copy your Assignment 4 KEY_SW.ko into the drivers folder."
        exit 1
    fi
    echo "--- Installing Assignment 4 KEY/SW driver ---"
    if ! insmod ./drivers/KEY_SW.ko; then
        echo "ERROR: Could not load drivers/KEY_SW.ko."
        echo "If the module was built for another kernel, rebuild it on this board:"
        echo "  cd drivers && make"
        exit 1
    fi
    if [ ! -e /dev/KEY ] || [ ! -e /dev/SW ]; then
        echo "ERROR: Expected /dev/KEY and /dev/SW device nodes were not created."
        exit 1
    fi
    cd part5 || exit 1
    make clean && make || exit 1
    echo "---  Running Part 5 (Ctrl-C to quit)           ---"
    echo "---  KEY0: faster  KEY1: slower                 ---"
    echo "---  KEY2: more Xs KEY3: fewer Xs              ---"
    echo "---  All switches OFF: show connecting lines   ---"
    echo "---  Any switch ON: hide connecting lines      ---"
    echo "Running in 3..."
    sleep 1
    echo "2..."
    sleep 1
    echo "1..."
    sleep 1
    exec ./part5
    ;;
additional) echo "---     Setting up Additional Part     ---"
    if ! ls partAdditional/*.c > /dev/null 2>&1; then
        echo "ERROR: No Additional Part source code found."
        exit 1
    fi
    rmmod KEY_SW 2>/dev/null || true
    rmmod KEY 2>/dev/null || true
    rmmod SW 2>/dev/null || true
    if [ ! -f drivers/KEY_SW.ko ]; then
        echo "ERROR: drivers/KEY_SW.ko was not found."
        exit 1
    fi
    if ! insmod ./drivers/KEY_SW.ko; then
        echo "ERROR: Could not load drivers/KEY_SW.ko"
        exit 1
    fi
    if [ ! -e /dev/KEY ] || [ ! -e /dev/SW ]; then
        echo "ERROR: /dev/KEY and /dev/SW were not created."
        exit 1
    fi
    cd partAdditional || exit 1
    make || exit 1
    echo "---  Running Additional Part (Ctrl-C to quit) ---"
    echo "KEY0/1: Faster/Slower  KEY2/3: More/Fewer shapes"
    echo "SW0: Rect  SW1: Triangle  SW2: Square  SW3: Mixed"
    echo "SW9: Hide shape outlines (X markers stay visible)"
    exec ./additional
    ;;
"clean") echo "-- Cleaning Up! --"
   rmmod KEY_SW 2>/dev/null || true
   rmmod SW
   rmmod KEY
   (cd partAdditional && make unload)
   cd part1  && make clean && cd ../
   cd part2  && make clean && cd ../
   cd part3  && make clean && cd ../
   cd part4  && make clean && cd ../
   cd part5  && make clean && cd ../
   cd partAdditional && make clean && cd ../
   ;;
"submit") echo "---   Creating submission archive.  Please upload to Quercus!   ---"
   rmmod KEY_SW 2>/dev/null || true
   rmmod -f SW
   rmmod -f KEY
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd part5 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   tar -cjvf e5_$(date "+%Y.%m.%d-%H.%M.%S").tar.bz2 part1 part2 part3 part4 part5 partAdditional include drivers README runall.sh
   ;;
*) echo "Usage : ./runall.sh 1|2|3|4|5|additional|clean|submit"
   ;;

esac

