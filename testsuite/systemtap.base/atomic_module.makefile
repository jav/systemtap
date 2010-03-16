obj-m := atomic_module.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CLEAN_FILES := $(shell echo *.mod.c *.ko *.o .*.cmd *~ *.sgn)
CLEAN_FILES += Module.markers modules.order Module.symvers
CLEAN_DIRS  := .tmp_versions

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -f $(CLEAN_FILES)
	rm -rf $(CLEAN_DIRS)
