obj-m := uprobes.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DEPENDENCIES := $(shell echo uprobes.[ch] uprobes_*.[ch])
DEPENDENCIES += $(shell echo ../uprobes2/uprobes.[ch] ../uprobes2/uprobes_*.[ch])
DEPENDENCIES += Makefile

CLEAN_FILES := $(shell echo *.mod.c *.ko *.o .*.cmd *~ *.sgn)
CLEAN_FILES += Module.markers modules.order Module.symvers
CLEAN_DIRS  := .tmp_versions

LIBEXECDIR = ../../../../libexec/systemtap
CERTFILE = ~/.systemtap/ssl/server/stap.cert
CERTDB = ../../../../etc/systemtap/staprun

# Build the module and sign it.
# Ensure that the generated files are writeable by the group which
# owns this build directory. This is so that the stap-server service
# can rebuild the module, if necessary.
# Make sure that all the required tools and resources are available
# before attempting to sign the module. 
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	if test -x $(LIBEXECDIR)/stap-sign-module; then \
		if test -x `which certutil`; then \
			if ! test -f $(CERTFILE); then \
				$(LIBEXECDIR)/stap-gen-cert; \
			fi; \
			if ! test -d $(CERTDB); then \
				mkdir -p $(CERTDB); \
			fi; \
			certutil -A -n stap-server -d $(CERTDB) -i $(CERTFILE) -t "P,P,P"; \
			for f in *.ko; do \
				if test ! -e $$f.sgn -o $$f.sgn -ot $$f; then \
					$(LIBEXECDIR)/stap-sign-module $$f; \
				fi \
			done \
		fi \
	fi
	group=`stat -c %G $(PWD)`; \
	for f in $(CLEAN_FILES); do \
		test ! -f $$f && continue; \
		chgrp -f $$group $$f; \
		chmod -f 664 $$f; \
	done; \
	for d in $(CLEAN_DIRS); do \
		test ! -d $$d && continue; \
		chgrp -f $$group $$d; \
		chmod -f 775 $$d; \
		for dd in `find $$d -type d`; do \
			chgrp -f $$group $$dd; \
			chmod -f 775 $$dd; \
		done; \
		for f in `find $$d -type f`; do \
			chgrp -f $$group $$f; \
			chmod -f 664 $$f; \
		done; \
	done

# This target is used with "make -q" to see whether a "real" build is needed.
uprobes.ko: $(DEPENDENCIES)
	@echo uprobes.ko is not a valid target.  See Makefile.

clean:
	rm -f $(CLEAN_FILES)
	rm -rf $(CLEAN_DIRS)
