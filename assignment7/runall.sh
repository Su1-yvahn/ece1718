#!/bin/bash


if [ $# -lt 1 ]
then
        echo "Usage : ./runall.sh 1|2|3|4|additional|clean|submit"
        exit
fi

case "$1" in

1)  echo "---                Setting up Part 1               ---"
    (cd partAdditional && make unload)
    cd part1 && make clean && make
    echo "---          Running Part 1 (Ctrl-C to quit)       ---"
    exec ./part1
    ;;
2)  echo "---                Setting up Part 2               ---"
    (cd partAdditional && make unload)
    cd part2 && make clean && make
    rmmod -f accel
    echo "---     Installing Part 2 accelerometer driver     ---"
    insmod accel.ko
    echo "---  Printing accelerometer data: cat /dev/accel   ---"
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    echo ""
    echo "---  Running Part 2 user program (Ctrl-C to quit)  ---"
    exec ./part2
    ;;
3)  echo "---                Setting up Part 3               ---"
    (cd partAdditional && make unload)
    cd part3 && make clean && make
    rmmod -f accel
    echo "---     Installing Part 3 accelerometer driver     ---"
    insmod accel.ko
    sleep 2
    echo "---  Printing accelerometer data: cat /dev/accel   ---"
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    echo "---  Printing device id (to serial terminal): echo device > /dev/accel  ---"
    echo "device" > /dev/accel
    sleep 2
    echo "---  Running Part 3 user program (Ctrl-C to quit)  ---"
    exec ./part3
    ;;
4)  echo "---              Setting up Part 4                 ---"
    (cd partAdditional && make unload)
    cd part4 || exit 1
    make clean || exit 1
    make || exit 1
    if [ ! -x ./part4 ]; then
        echo "ERROR: Part 4 executable was not generated."
        exit 1
    fi
    rmmod -f accel
    echo "---     Installing Part 4 accelerometer driver     ---"
    insmod accel.ko || exit 1
    echo "---  Printing accelerometer data: cat /dev/accel   ---"
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    cat /dev/accel
    sleep 2
    echo "---  Running Part 4 user program (Ctrl-C to quit)  ---"
    exec ./part4
    ;;
additional) echo "---     Setting up Additional Part     ---"
    if ! ls partAdditional/*.c > /dev/null 2>&1; then
        echo "partAdditional has no .c files. Put your additional part code there first (see README)."
        exit
    fi
    rmmod -f accel
    cd partAdditional && make clean && make && make load || exit
    if [ ! -e /dev/accel ]; then
        echo "No /dev/accel. Put the accel_kernel_module.c your program uses into partAdditional (see README)."
        exit
    fi
    echo ""
    echo "---   To uninstall driver: ./runall.sh clean     ---"
    PROG=$(make -s name)
    if [ -n "$PROG" ]; then
        echo "---   Running $PROG (Ctrl-C to quit)   ---"
        exec ./$PROG
    fi
    ;;
"clean") echo "-- Cleaning Up! --"
    rmmod -f accel
    (cd partAdditional && make unload)
    cd part1  && make clean && cd ../
    cd part2  && make clean && cd ../
    cd part3  && make clean && cd ../
    cd part4  && make clean && cd ../
    cd partAdditional && make clean && cd ../
    ;;
"submit") echo "---   Creating submission archive.  Please upload to Quercus!   ---"
    rmmod -f accel
    (cd partAdditional && make unload)
    cd part1 && make clean && cd ../
    cd part2 && make clean && cd ../
    cd part3 && make clean && cd ../
    cd part4 && make clean && cd ../
    cd partAdditional && make clean && cd ../
    tar -cjvf e7_$(date "+%Y.%m.%d-%H.%M.%S").tar.bz2 part1 part2 part3 part4 partAdditional include README runall.sh ADXL345.c accel_wrappers.c
    ;;
*)  echo "Usage : ./runall.sh 1|2|3|4|additional|clean|submit"
    ;;

esac

