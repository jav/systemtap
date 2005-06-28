all: stpd stp_merge

stpd: stpd.c librelay.c
	gcc -Wall -O3 -o stpd stpd.c librelay.c -lpthread

stp_merge: stp_merge.c
	gcc -Wall -O3 -o stp_merge stp_merge.c

clean:
	/bin/rm -f stpd stp_merge *.o *~
