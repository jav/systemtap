CFLAGS = -Wall -std=gnu99 -D_GNU_SOURCE -fexceptions -Wall -Werror -Wshadow -Wunused

all: staprun stp_merge

staprun: staprun.c mainloop.c relay.c ctl.c symbols.c ../transport/transport_msgs.h staprun.h
	gcc -O3 $(CFLAGS) -o staprun staprun.c mainloop.c symbols.c ctl.c relay.c relay_old.c -lpthread

stp_merge: stp_merge.c
	gcc -O3 $(CFLAGS) -o stp_merge stp_merge.c

debug: staprun.c mainloop.c relay.c ctl.c symbols.c ../transport/transport_msgs.h staprun.h
	gcc -g -D DEBUG $(CFLAGS) -o staprun staprun.c mainloop.c symbols.c ctl.c relay.c relay_old.c -lpthread	

clean:
	/bin/rm -f staprun stp_merge *.o *~
