/* program to exercise spec_example.stp */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define BOGUS NULL
#define MAXSIZE 512
static char buf1[MAXSIZE];
static char buf2[MAXSIZE];

main (int argc, char ** argv)
{
	int fbad = open("/tmp/junky_bad", O_CREAT|O_RDWR, 0660);
	int fokay = open("/tmp/junky_good", O_CREAT|O_RDWR, 0660);

	if (fbad < 0) return(EXIT_FAILURE);
	if (fokay < 0) return(EXIT_FAILURE);
	
	write(fokay, buf1, MAXSIZE);
	write(fokay, buf1, MAXSIZE);
	write(fbad, buf2, MAXSIZE);
	write(fokay, buf2, MAXSIZE);
	write(fokay, buf1, MAXSIZE);
	write(fbad, buf1, MAXSIZE);
	write(fbad, BOGUS, MAXSIZE);
	write(fokay, buf1, MAXSIZE);

	return(EXIT_SUCCESS);
}
