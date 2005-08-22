all: ttest itest

ttest: ttest.c
	gcc -Wall -O3 -o ttest ttest.c

itest: itest.c
	gcc -Wall -O3 -o itest itest.c

clean:
	/bin/rm -f itest ttest stpd_cpu* xxx* 
