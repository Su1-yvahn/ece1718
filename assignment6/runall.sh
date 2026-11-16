#!/bin/bash


if [ $# -lt 1 ]
then
        echo "Usage : ./runall.sh 1|2|3|4|5|6|additional|clean|submit"
        exit
fi

case "$1" in

1)  echo "---           Setting up Part 1             ---"
    (cd partAdditional && make unload)
    cd part1 && make clean && make
    echo "---     Installing Part 1 video driver      ---"
    rmmod -f video
    insmod video.ko
    echo "---  Running Part 1 (press Enter to quit)   ---"
    exec ./part1
    ;;
2)  echo "---           Setting up Part 2             ---"
    (cd partAdditional && make unload)
    cd part2 && make clean && make
    echo "---     Installing Part 2 video driver      ---"
    rmmod -f video
    insmod video.ko
    echo "---  Running Part 2 (press Enter to quit)   ---"
    exec ./part2
    ;;
3)  echo "---           Setting up Part 3             ---"
    (cd partAdditional && make unload)
    cd part3 && make clean && make
    echo "---     Installing Part 3 video driver      ---"
    rmmod -f video
    insmod video.ko
    echo "---    Running Part 3 (Ctrl-C to quit)      ---"
    exec ./part3
    ;;
4)  echo "---           Setting up Part 4             ---"
    (cd partAdditional && make unload)
    cd part4 && make clean && make
    echo "---     Installing Part 4 video driver      ---"
    rmmod -f video
    insmod video.ko
    echo "---    Running Part 4 (Ctrl-C to quit)      ---"
    exec ./part4
    ;;
5)  echo "---           Setting up Part 5             ---"
    (cd partAdditional && make unload)
    # Unload older KEY/SW modules and the previous video driver.
    rmmod KEY_SW 2>/dev/null || true
    rmmod KEY 2>/dev/null || true
    rmmod SW 2>/dev/null || true
    rmmod video 2>/dev/null || true

    # Rebuild the Exercise 5 KEY/SW driver for this board's running kernel.
    (cd drivers && make) || exit 1
    if ! insmod ./drivers/KEY_SW.ko; then
        echo "ERROR: Could not load drivers/KEY_SW.ko"
        exit 1
    fi
    if [ ! -e /dev/KEY ] || [ ! -e /dev/SW ]; then
        echo "ERROR: /dev/KEY or /dev/SW was not created"
        exit 1
    fi

    (cd part4 && make clean && make) || exit 1
    if ! insmod ./part4/video.ko; then
        echo "ERROR: Could not load the Part 4 video driver"
        exit 1
    fi
    (cd part5 && make clean && make part5) || exit 1
    echo "---  Running Part 5 (Ctrl-C to quit)           ---"
    echo "---  KEY0: faster, KEY1: slower                ---"
    echo "---  KEY2: more objects, KEY3: fewer objects   ---"
    echo "---  All switches OFF: show connecting lines  ---"
    echo "---  Any switch ON: hide connecting lines     ---"
    exec ./part5/part5
    ;;
6)  echo "---           Setting up Part 6             ---"
    (cd partAdditional && make unload)
    # Unload older KEY/SW modules and the previous video driver.
    rmmod KEY_SW 2>/dev/null || true
    rmmod KEY 2>/dev/null || true
    rmmod SW 2>/dev/null || true
    rmmod video 2>/dev/null || true

    # Rebuild the Exercise 5 KEY/SW driver for this board's running kernel.
    (cd drivers && make) || exit 1
    if ! insmod ./drivers/KEY_SW.ko; then
        echo "ERROR: Could not load drivers/KEY_SW.ko"
        exit 1
    fi
    if [ ! -e /dev/KEY ] || [ ! -e /dev/SW ]; then
        echo "ERROR: /dev/KEY or /dev/SW was not created"
        exit 1
    fi

    (cd part6 && make clean && make) || exit 1
    if ! insmod ./part6/video.ko; then
        echo "ERROR: Could not load the Part 6 video driver"
        exit 1
    fi
    echo "---  Running Part 6 (Ctrl-C to quit)           ---"
    echo "---  KEY0: faster, KEY1: slower                ---"
    echo "---  KEY2: more objects, KEY3: fewer objects   ---"
    echo "---  All switches OFF: show connecting lines  ---"
    echo "---  Any switch ON: hide connecting lines     ---"
    echo "---  Displays Frames rendered counter at top-left ---"
    exec ./part6/part6
    ;;
additional) echo "---     Setting up Additional Part     ---"
    rmmod KEY_SW 2>/dev/null || true
    rmmod KEY 2>/dev/null || true
    rmmod SW 2>/dev/null || true
    rmmod video 2>/dev/null || true

    (cd drivers && make) || exit 1
    insmod ./drivers/KEY_SW.ko || exit 1
    if [ ! -e /dev/KEY ] || [ ! -e /dev/SW ]; then
        echo "ERROR: KEY/SW devices not created"
        exit 1
    fi

    (cd partAdditional && make clean && make) || exit 1
    insmod ./partAdditional/video.ko || exit 1
    echo "---  Running Additional Part (Ctrl-C to quit) ---"
    echo "---  SW0: rectangle, SW1: triangle, SW2: square ---"
    echo "---  SW3: all shapes, SW9: hide shape outlines ---"
    echo "---  KEY0/1: speed, KEY2/3: object/shape count ---"
    exec ./partAdditional/additional
    ;;
"clean") echo "---  Cleaning Up!  ---"
   rmmod KEY_SW 2>/dev/null || true
   rmmod -f SW
   rmmod -f KEY
   rmmod -f video
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd part5 && make clean && cd ../
   cd part6 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   (cd drivers && make clean)
   ;;
"submit") echo "---   Creating submission archive.  Please upload to Quercus!   ---"
   rmmod KEY_SW 2>/dev/null || true
   rmmod -f SW
   rmmod -f KEY
   rmmod -f video
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd part5 && make clean && cd ../
   cd part6 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   (cd drivers && make clean)
   tar -cjvf e6_$(date "+%Y.%m.%d-%H.%M.%S").tar.bz2 part1 part2 part3 part4 part5 part6 partAdditional include drivers README runall.sh
   ;;
*) echo "Usage : ./runall.sh 1|2|3|4|5|6|additional|clean|submit"
   ;;

esac

