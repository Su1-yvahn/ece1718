#!/bin/bash


if [ $# -lt 1 ]
then
        echo "Usage : ./runall.sh 1 | 2 | 3 | 4 | additional | clean | submit"
        exit
fi

case "$1" in

1)  echo "--- Setting up Part 1 ---"
    (cd partAdditional && make unload)
    rmmod -f stopwatch
    cd part1 && make clean && make
    echo ""
    echo "---    Installing part 1 stopwatch driver     ---"
    insmod stopwatch.ko
    echo ""
    echo "--- Printing time: cat /dev/stopwatch ---"
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 1
    echo ""
    echo "--- To uninstall stopwatch driver: ./runall.sh clean ---"
    ;;
2)  echo "---          Setting up Part 2              ---"
    (cd partAdditional && make unload)
    rmmod -f stopwatch
    cd part2 && make clean && make
    echo ""
    echo "---     Installing part 2 stopwatch driver     ---"
    insmod stopwatch.ko
    echo ""
    echo "--- Printing time: cat /dev/stopwatch ---"
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    echo ""
    echo "--- Pausing: echo stop > /dev/stopwatch ---"
    echo stop > /dev/stopwatch
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    echo ""
    echo "--- Resuming: echo run > /dev/stopwatch ---"
    echo run > /dev/stopwatch
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    echo ""
    echo "--- Turning on HEX display: echo disp > /dev/stopwatch ---"
    echo disp > /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    echo ""
    echo "--- Turning off HEX display: echo nodisp > /dev/stopwatch ---"
    echo nodisp > /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    cat /dev/stopwatch
    sleep 2
    echo ""
    echo "--- Turning on HEX display: echo disp > /dev/stopwatch ---"
    echo disp > /dev/stopwatch
    sleep 3
    echo ""
    echo "--- To uninstall stopwatch driver: ./runall.sh clean  ---"
    ;;
3)  echo "---          Setting up Part 3                  ---"
    (cd partAdditional && make unload)
    rmmod -f stopwatch
    rmmod -f SW
    rmmod -f KEY
    rmmod -f LEDR
    cd part2 && make clean && make
    echo ""
    echo "---     Installing Part 2 stopwatch driver      ---"
    insmod stopwatch.ko
    echo ""
    echo "---     Installing built in KEY, SW, and LEDR drivers                   ---"
    echo "---     (if you prefer to use your own drivers, modify this script as   ---"
    echo "---     needed and include your compiled drivers with your submission)  ---"
    insmod /home/root/Linux_Libraries/drivers/SW.ko
    insmod /home/root/Linux_Libraries/drivers/KEY.ko
    insmod /home/root/Linux_Libraries/drivers/LEDR.ko
    cd ../part3 && make clean && make
    echo ""
    echo "---     Running Part 3 (Ctrl-C to quit)          ---"
    exec ./part3
    ;;
4)  echo "---            Setting up Part 4                 ---"
    (cd partAdditional && make unload)
    rmmod -f stopwatch
    rmmod -f SW
    rmmod -f KEY
    rmmod -f LEDR
    cd part2 && make clean && make
    echo ""
    echo "---     Installing Part 2 stopwatch driver      ---"
    insmod stopwatch.ko
    echo ""
    echo "---     Installing built in KEY, SW, and LEDR drivers                   ---"
    echo "---     (if you prefer to use your own drivers, modify this script as   ---"
    echo "---     needed and include your compiled drivers with your submission)  ---"
    insmod /home/root/Linux_Libraries/drivers/SW.ko
    insmod /home/root/Linux_Libraries/drivers/KEY.ko
    insmod /home/root/Linux_Libraries/drivers/LEDR.ko
    cd ../part4 && make clean && make
    echo ""
    echo "---     Running Part 4 (Ctrl-C to quit)          ---"
    exec ./part4
    ;;
additional) echo "---          Setting up Additional Part         ---"
    if ! ls partAdditional/*.c > /dev/null 2>&1; then
        echo "partAdditional has no .c files. Put your additional part code there first (see README)."
        exit
    fi
    rmmod -f stopwatch
    rmmod -f SW
    rmmod -f KEY
    rmmod -f LEDR
    (cd partAdditional && make unload)
    (cd part2 && make clean && make)
    cd partAdditional && make clean && make && make load || exit
    # Load the part 2 stopwatch driver, unless your own module already made /dev/stopwatch
    [ -e /dev/stopwatch ] || insmod ../part2/stopwatch.ko
    echo ""
    echo "---     Installing built in KEY, SW, and LEDR drivers     ---"
    insmod /home/root/Linux_Libraries/drivers/SW.ko
    insmod /home/root/Linux_Libraries/drivers/KEY.ko
    insmod /home/root/Linux_Libraries/drivers/LEDR.ko
    echo ""
    echo "---   To uninstall drivers: ./runall.sh clean     ---"
    PROG=$(make -s name)
    if [ -n "$PROG" ]; then
        echo "---   Running $PROG (Ctrl-C to quit)   ---"
        exec ./$PROG
    fi
    ;;
"clean") echo "---   Cleaning Up!   ---"
    rmmod -f stopwatch
    rmmod -f SW
    rmmod -f KEY
    rmmod -f LEDR
    (cd partAdditional && make unload)
    cd part1 && make clean && cd ../
    cd part2 && make clean && cd ../
    cd part3 && make clean && cd ../
    cd part4 && make clean && cd ../
    cd partAdditional && make clean && cd ../
   ;;
"submit") echo "---   Creating submission archive.  Please upload to Quercus!   ---"
   rmmod -f stopwatch
   rmmod -f SW
   rmmod -f KEY
   rmmod -f LEDR
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   tar -cjvf e4_$(date "+%Y.%m.%d-%H.%M.%S").tar.bz2 part1 part2 part3 part4 partAdditional include README runall.sh
   ;;

*) echo "Usage : ./runall.sh 1 | 2 | 3 | 4 | additional | clean | submit"
   ;;

esac

