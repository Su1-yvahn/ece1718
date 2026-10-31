obj-m += KEY_SW.o
obj-m += LEDR_HEX.o
obj-m += stopwatch.o

.PHONY: all clean

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean