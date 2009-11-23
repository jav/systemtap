obj-m := uprobes.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DEPENDENCIES := $(shell echo uprobes.[ch] uprobes_*.[ch])
DEPENDENCIES += $(shell echo ../uprobes2/uprobes.[ch] ../uprobes2/uprobes_*.[ch])
DEPENDENCIES += Makefile

CLEAN_FILES := $(shell echo *.mod.c *.ko *.o .*.cmd *~ *.sgn)
CLEAN_FILES += Module.markers modules.order Module.symvers
CLEAN_DIRS  := .tmp_versions

# Build the module and sign it.
# Make sure that all of the generated files belong the group stap-server
# and are writable by that group. This is so that the stap-server service
# can build or rebuild the module, if necessary.
# If the chgrp and/or chmod commands fail, it is because we are not the
# owner of the file for directory. In this case the file or directory
# was created during a previous build and already has the correct permissions.
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	if test -f ../../../../bin/stap-sign-module; then \
		for f in *.ko; do \
			if test ! -e $$f.sgn -o $$f.sgn -ot $$f; then \
				../../../../bin/stap-sign-module $$f; \
			fi \
		done \
	fi
	if getent group stap-server >/dev/null; then \
		for f in $(CLEAN_FILES); do \
			test ! -f $$f && continue; \
			chgrp stap-server $$f 2>/dev/null; \
			chmod 664 $$f 2>/dev/null; \
		done; \
		for d in $(CLEAN_DIRS); do \
			test ! -d $$d && continue; \
			chgrp stap-server $$d 2>/dev/null; \
			chmod 775 $$d 2>/dev/null; \
			for dd in `find $$d -type d`; do \
				chgrp stap-server $$dd 2>/dev/null; \
				chmod 775 $$dd 2>/dev/null; \
			done; \
			for f in `find $$d -type f`; do \
				chgrp stap-server $$f 2>/dev/null; \
				chmod 664 $$f 2>/dev/null; \
			done; \
		done; \
	fi

# This target is used with "make -q" to see whether a "real" build is needed.
uprobes.ko: $(DEPENDENCIES)
	@echo uprobes.ko is not a valid target.  See Makefile.

clean:
	rm -f $(CLEAN_FILES)
	rm -rf $(CLEAN_DIRS)
