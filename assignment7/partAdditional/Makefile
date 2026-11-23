# No need to edit this file. Put your .c files in this folder and run  ./runall.sh additional
#   - accel_kernel_module.c is built with ../ADXL345.c into the accel.ko driver (/dev/accel)
#   - a file containing main() is built with ../accel_wrappers.c into a user program (<name>)
#   - any other .c file is linked into the user program as a helper
# Targets used by runall.sh: unload, load (insmod accel.ko), name (prints your program's name)

ifneq ($(KERNELRELEASE),)
# Read by the kernel build system
obj-m := accel.o
accel-objs := accel_kernel_module.o ../ADXL345.o
# also accept #include "include/x.h" and "x.h", not only "../include/x.h"
ccflags-y := -I$(M)/.. -I$(M)/../include
else

INCLUDES=-I. -I../include -I..
FLAGS=-Wall

ALL_C   := $(filter-out accel_kernel_module.c,$(wildcard *.c))
# make counts parentheses inside $(...), so the '(' for grep comes from a variable
PAREN   := (
USRCS   := $(if $(ALL_C),$(shell grep -l 'main *$(PAREN)' $(ALL_C)))
HELPERS := $(filter-out $(USRCS),$(ALL_C))
UBINS   := $(USRCS:.c=)
DRIVER  := $(wildcard accel_kernel_module.c)
KBUILD   = make -C /lib/modules/$(shell uname -r)/build M=$(PWD)

.PHONY: all driver clean unload load name

all: $(if $(DRIVER),driver) $(UBINS)

driver:
	$(KBUILD) modules KCPPFLAGS="-DKERNEL"

ifneq ($(UBINS),)
$(UBINS): %: %.c $(HELPERS) ../accel_wrappers.c
	gcc $(FLAGS) $(INCLUDES) -o $@ $< $(HELPERS) ../accel_wrappers.c
endif

clean:
	$(KBUILD) clean
	rm -f $(UBINS) ../ADXL345.o

unload:
	rmmod -f accel 2>/dev/null; true

load:
	$(if $(DRIVER),insmod accel.ko)

name:
	@echo $(firstword $(UBINS))

endif
