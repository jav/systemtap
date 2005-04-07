# Makefile
#
#
# make -C path/to/kernel/src M=`pwd` modules STP_RUNTIME=path_to_systemtap_rt

CFLAGS += -I $(STP_RUNTIME) -I $(STP_RUNTIME)/relayfs -D KALLSYMS_LOOKUP_NAME=$(KALLSYMS_LOOKUP_NAME) \
	-D KALLSYMS_LOOKUP=$(KALLSYMS_LOOKUP) -D KTA=$(KTA)
obj-m := dtr.o

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions
