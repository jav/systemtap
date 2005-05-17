#
# relayfs Makefile
#

PWD     := $(shell pwd)
KDIR   := /lib/modules/$(shell uname -r)/build

relayfs-y := relay.o inode.o buffers.o

obj-m += relayfs.o

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions

