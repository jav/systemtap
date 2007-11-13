obj-m := uprobes.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DEPENDENCIES := $(shell echo uprobes_arch.[ch] uprobes.[ch] uprobes_*.[ch])
DEPENDENCIES += Makefile $(KDIR)/Module.symvers

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

# This target is used with "make -q" to see whether a "real" build is needed.
uprobes.ko: $(DEPENDENCIES)
	@echo uprobes.ko is not a valid target.  See Makefile.

clean:
	rm -f *.mod.c *.ko *.o .*.cmd *~
	rm -rf .tmp_versions
