#!/bin/bash


if [ $# -lt 1 ]
then
        echo "Usage : ./runall.sh 1 | 2 | 3 | 4 | additional | clean | submit"
        exit
fi

case "$1" in

1)  echo "--- Setting up Part 1 ---"
    (cd partAdditional && make unload)
    rmmod -f chardev
    cd part1 && make clean && make
    echo ""
    echo "---  Installing part 1 chardev driver  ---"
    insmod chardev.ko
    echo ""
    echo "---  Running part1 (Ctrl-C to quit)          ---"
    echo "---  To uninstall driver: ./runall.sh clean  ---"
    exec ./part1
    echo ""
    ;;
2)  echo "---          Setting up Part 2              ---"
    (cd partAdditional && make unload)
    rmmod -f chardev
    rmmod -f KEY_SW
    rmmod -f LEDR_HEX
    cd part2 && make clean && make
    echo ""
    echo "---     Installing part 2 KEY_SW driver     ---"
    insmod KEY_SW.ko
    echo ""
    echo "---   Printing switch values: cat /dev/SW   ---"
    cat /dev/SW
    echo ""
    echo "---   To print key values: cat /dev/KEY     ---"
    echo ""
    echo "--- To uninstall driver: ./runall.sh clean  ---"
    ;;
3)  echo "---          Setting up Part 3                  ---"
    (cd partAdditional && make unload)
    rmmod -f chardev
    rmmod -f KEY_SW
    rmmod -f LEDR_HEX
    cd part3 && make clean && make
    echo ""
    echo "---      Installing part 3 HEX_LEDR driver       ---"
    insmod LEDR_HEX.ko
    echo ""
    echo "---     Testing LEDs: echo 2AA > /dev/LEDR'      ---"
    echo 2AA > /dev/LEDR
    echo ""
    echo "---  Testing HEX display: echo 123456 > /dev/HEX ---"
    echo 123456 > /dev/HEX
    echo ""
    echo "---   To uninstall driver: ./runall.sh clean     ---"
    ;;
4)  echo "---             Setting up Part 4                  ---"
    (cd partAdditional && make unload)
    rmmod -f chardev
    rmmod -f KEY_SW
    rmmod -f LEDR_HEX
    cd part2 && make clean && make
    insmod KEY_SW.ko
    cd ..
    cd part3 && make clean && make
    insmod LEDR_HEX.ko
    cd ..
    cd part4 && make clean && make
    echo ""
    echo "---      Running Part 4 (Ctrl-C to quit)        ---"
    echo "--- Press any key to add the switch value to    ---"
    echo "--- the accumulator and display on the LEDs.    ---"
    echo "--- To reset the accumulator, set the switches  ---"
    echo "--- to zero and press any key.                  ---"
    exec ./part4
    ;;
additional) echo "---          Setting up Additional Part         ---"
    if ! ls partAdditional/*.c > /dev/null 2>&1; then
        echo "partAdditional has no .c files. Put your additional part code there first (see README)."
        exit
    fi
    rmmod -f chardev
    rmmod -f KEY_SW
    rmmod -f LEDR_HEX
    (cd partAdditional && make unload)
    (cd part2 && make clean && make)
    (cd part3 && make clean && make)
    cd partAdditional && make clean && make && make load || exit
    # Load the part 2 and part 3 drivers, unless your own module already made those /dev files
    [ -e /dev/KEY ] || [ -e /dev/SW ] || insmod ../part2/KEY_SW.ko
    [ -e /dev/LEDR ] || [ -e /dev/HEX ] || insmod ../part3/LEDR_HEX.ko
    echo ""
    echo "---   To uninstall drivers: ./runall.sh clean     ---"
    PROG=$(make -s name)
    if [ -n "$PROG" ]; then
        echo "---   Running $PROG (Ctrl-C to quit)   ---"
        exec ./$PROG
    fi
    ;;
"clean") echo "---   Cleaning Up!   ---"
   rmmod -f chardev
   rmmod -f KEY_SW
   rmmod -f LEDR_HEX
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   ;;
"submit") echo "---   Creating submission archive.  Please upload to Quercus!   ---"
   rmmod -f chardev
   rmmod -f KEY_SW
   rmmod -f LEDR_HEX
   (cd partAdditional && make unload)
   cd part1 && make clean && cd ../
   cd part2 && make clean && cd ../
   cd part3 && make clean && cd ../
   cd part4 && make clean && cd ../
   cd partAdditional && make clean && cd ../
   tar -cjvf e3_$(date "+%Y.%m.%d-%H.%M.%S").tar.bz2 part1 part2 part3 part4 partAdditional include README runall.sh
   ;;

*) echo "Usage : ./runall.sh 1 | 2 | 3 | 4 | additional | clean | submit"
   ;;

esac

