#
# relayfs Makefile
#

CFLAGS += -I $(RELAYFS_INCLUDE)

obj-m += relayfs.o

relayfs-y := relay.o inode.o buffers.o

clean:
	/bin/rm -rf *.o *.ko *~ *.mod.c .*.cmd .tmp_versions
