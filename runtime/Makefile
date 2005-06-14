#
# Makefile for SystemTap runtime and example probes
#

all:
	make -w -C relayfs
	make -w -C transport
	make -w -C stpd
	cd probes; ./build

clean:
	make -w -C relayfs clean
	make -w -C transport clean
	make -w -C stpd clean
	cd probes; ./build clean
