CFLAGS = -Wall -std=gnu99 -D_GNU_SOURCE -fexceptions -Wall -Werror -Wshadow -Wunused

all: staprun stp_merge stp_dump

staprun: stpd.c librelay.c symbols.c ../transport/transport_msgs.h librelay.h
	gcc -O3 $(CFLAGS) -o staprun stpd.c librelay.c symbols.c -lpthread

stp_merge: stp_merge.c
	gcc -O3 $(CFLAGS) -o stp_merge stp_merge.c

stp_dump: stp_dump.c
	gcc -O3 $(CFLAGS) -o stp_dump stp_dump.c

debug: stpd.c librelay.c symbols.c ../transport/transport_msgs.h librelay.h
	gcc -g -D DEBUG $(CFLAGS) -o staprun stpd.c librelay.c symbols.c -lpthread	

clean:
	/bin/rm -f staprun stp_merge *.o *~
