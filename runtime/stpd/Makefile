all: stpd stp_merge stp_dump

stpd: stpd.c librelay.c ../transport/transport_msgs.h librelay.h
	gcc -Wall -O3 -o stpd stpd.c librelay.c -lpthread

stp_merge: stp_merge.c
	gcc -Wall -O3 -o stp_merge stp_merge.c

stp_dump: stp_dump.c
	gcc -Wall -O3 -o stp_dump stp_dump.c

debug: stpd.c librelay.c ../transport/transport_msgs.h librelay.h
	gcc -Wall -g -D DEBUG  -o stpd stpd.c librelay.c -lpthread	

clean:
	/bin/rm -f stpd stp_merge *.o *~
