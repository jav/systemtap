all: stpd

stpd: stpd.c librelay.c
	$(CC) -Wall -O3 -o stpd stpd.c librelay.c -lpthread

clean:
	/bin/rm -f stpd stpd.o librelay.o *~
