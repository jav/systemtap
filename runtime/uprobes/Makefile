obj-m := uprobes.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DEPENDENCIES := $(shell echo uprobes.[ch] uprobes_*.[ch])
DEPENDENCIES += $(shell echo ../uprobes2/uprobes.[ch] ../uprobes2/uprobes_*.[ch])
DEPENDENCIES += Makefile

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	if test -f ../../../../bin/stap-sign-module; then \
		for f in *.ko; do \
			if test ! -e $$f.sgn -o $$f.sgn -ot $$f; then \
				../../../../bin/stap-sign-module $$f; \
			fi \
		done \
	fi

# This target is used with "make -q" to see whether a "real" build is needed.
uprobes.ko: $(DEPENDENCIES)
	@echo uprobes.ko is not a valid target.  See Makefile.

clean:
	rm -f *.mod.c *.ko *.o .*.cmd *~ *.sgn
	rm -f Module.markers modules.order Module.symvers
	rm -rf .tmp_versions
