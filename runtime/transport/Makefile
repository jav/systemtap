# Makes stp-control.ko

PWD     := $(shell pwd)
KDIR   := /usr/local/src/linux-$(shell uname -r)

stp-control-objs	:= control.o netlink.o

obj-m := stp-control.o

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions
