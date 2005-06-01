# Makefile

PWD     := $(shell pwd)
RT	:= $(PWD)/../..
KVERSION	:= $(shell uname -r)
KDIR	:= /lib/modules/$(KVERSION)/build include

KALLSYMS_LOOKUP_NAME	:= 0x$(firstword $(shell grep " kallsyms_lookup_name" /boot/System.map-$(KVERSION)))
KALLSYMS_LOOKUP	:= 0x$(firstword $(shell grep " kallsyms_lookup$$" /boot/System.map-$(KVERSION)))
KTA	:= 0x$(firstword $(shell grep "__kernel_text_address" /boot/System.map-$(KVERSION)))

EXTRA_CFLAGS := -I $(RT) -I $(RT)/relayfs -D KALLSYMS_LOOKUP_NAME=$(KALLSYMS_LOOKUP_NAME) -D KALLSYMS_LOOKUP=$(KALLSYMS_LOOKUP)

obj-m := tasklet.o


default:
	$(MAKE) V=1 -C $(KDIR) M=$(PWD) RT=$(RT) modules

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions
