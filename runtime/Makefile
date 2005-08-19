#
# Makefile for SystemTap runtime and example probes
#

all:
	make -w -C stpd
	cd probes; ./build

relayfs:
	make -w -C relayfs

clean:
	make -w -C relayfs clean
	make -w -C stpd clean
	cd probes; ./build clean
