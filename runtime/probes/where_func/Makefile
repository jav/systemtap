# Makefile

PWD     := $(shell pwd)
KVERSION	:= $(shell uname -r)
KDIR	:= /lib/modules/$(KVERSION)/build include

KALLSYMS_LOOKUP_NAME	:= $(firstword $(shell grep " kallsyms_lookup_name" /boot/System.map-$(KVERSION)))
KALLSYMS_LOOKUP	:= $(firstword $(shell grep " kallsyms_lookup$$" /boot/System.map-$(KVERSION)))
KTA	:= $(firstword $(shell grep "__kernel_text_address" /boot/System.map-$(KVERSION)))

CFLAGS += -I $(STP_RUNTIME) -I $(STP_RUNTIME)/relayfs -D KALLSYMS_LOOKUP_NAME=$(KALLSYMS_LOOKUP_NAME) \
	-D KALLSYMS_LOOKUP=$(KALLSYMS_LOOKUP)

obj-m := kprobe_where_funct.o

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules \
	KALLSYMS_LOOKUP_NAME=0x$(KALLSYMS_LOOKUP_NAME) \
	KALLSYMS_LOOKUP=0x$(KALLSYMS_LOOKUP)  KTA=0x$(KTA)\
	STP_RUNTIME=$(PWD)/../..

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions
