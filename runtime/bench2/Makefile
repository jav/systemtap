all: itest

itest: itest.c
	gcc -D_GNU_SOURCE -Wall -Wextra -Wstrict-prototypes -Werror -O3 -o itest itest.c -lpthread

clean:
	/bin/rm -f itest
