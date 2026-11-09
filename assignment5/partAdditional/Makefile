# No need to edit this file. Put your .c files in this folder and run  ./runall.sh additional
# Targets used by runall.sh: unload, load (insmod your modules), name (prints your program's name)
#   - a file containing module_init  is built as a kernel module (<name>.ko)
#   - a file containing main()       is built as a user program  (<name>)
#   - any other .c file is linked into the user programs as a helper

ifneq ($(KERNELRELEASE),)
# Read by the kernel build system: pick the module sources from this folder ($(M))
obj-m := $(notdir $(patsubst %.c,%.o,$(shell grep -ls module_init $(M)/*.c)))
# also accept #include "include/x.h" and "x.h", not only "../include/x.h"
ccflags-y := -I$(M)/.. -I$(M)/../include
else

INCLUDES=-I../include -I..
FLAGS=-Wall

ALL_C   := $(wildcard *.c)
KSRCS   := $(if $(ALL_C),$(shell grep -l module_init $(ALL_C)))
# make counts parentheses inside $(...), so the '(' for grep comes from a variable
PAREN   := (
USRCS   := $(if $(ALL_C),$(filter-out $(KSRCS),$(shell grep -l 'main *$(PAREN)' $(ALL_C))))
HELPERS := $(filter-out $(KSRCS) $(USRCS),$(ALL_C))
KMODS   := $(KSRCS:.c=)
UBINS   := $(USRCS:.c=)
KBUILD   = make -C /lib/modules/$(shell uname -r)/build M=$(PWD)

.PHONY: all driver clean unload load name

all: $(if $(KMODS),driver) $(UBINS)

driver:
	$(KBUILD) modules

ifneq ($(UBINS),)
$(UBINS): %: %.c $(HELPERS)
	gcc $(FLAGS) $(INCLUDES) -o $@ $< $(HELPERS)
endif

clean:
	$(KBUILD) clean
	rm -f $(UBINS)

unload:
	for m in $(KMODS); do rmmod -f $$m 2>/dev/null; done; true

load:
	for m in $(KMODS); do insmod $$m.ko; done

name:
	@echo $(firstword $(UBINS))

endif
