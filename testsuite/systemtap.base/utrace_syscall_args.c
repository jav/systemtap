#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

int main()
{
	int fd, ret;
	struct stat fs;
	void *r;
	int rc;

	/* create a file with something in it */
	fd = open("foobar", O_WRONLY|O_CREAT|O_TRUNC, 0600);
	lseek(fd, 1024, SEEK_SET);
	write(fd, "abcdef", 6);
	close(fd);

	fd = open("foobar", O_RDONLY);

	/* stat for file size */
	ret = fstat(fd, &fs);

	/* mmap file file, then unmap it. */
	r = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (r != MAP_FAILED)
		munmap(r, fs.st_size);
	close(fd);

	/* OK, try some system calls to see if we get the arguments
	 * correctly. */
#if (__LONG_MAX__ > __INT_MAX__)
	rc = syscall (__NR_dup, (unsigned long)-12345,
		      (unsigned long)0xffffffffffffffff,
		      (unsigned long)0xa5a5a5a5a5a5a5a5,
		      (unsigned long)0xf0f0f0f0f0f0f0f0,
		      (unsigned long)0x5a5a5a5a5a5a5a5a,
		      (unsigned long)0xe38e38e38e38e38e);
#else
	rc = syscall (__NR_dup, (unsigned long)-12345,
		      (unsigned long)0xffffffff,
		      (unsigned long)0xa5a5a5a5,
		      (unsigned long)0xf0f0f0f0,
		      (unsigned long)0x5a5a5a5a,
		      (unsigned long)0xe38e38e3);
#endif
#if (__LONG_MAX__ > __INT_MAX__)
	rc = syscall ((unsigned long)-1,
		      (unsigned long)0x1c71c71c71c71c71,
		      (unsigned long)0x0f0f0f0f0f0f0f0f,
		      (unsigned long)0xdb6db6db6db6db6d,
		      (unsigned long)0x2492492492492492,
		      (unsigned long)0xad6b5ad6b5ad6b5a,
		      (unsigned long)0xdef7ddef7ddef7dd);
#else
	rc = syscall ((unsigned long)-1,
		      (unsigned long)0x1c71c71c,
		      (unsigned long)0x0f0f0f0f,
		      (unsigned long)0xdb6db6db,
		      (unsigned long)0x24924924,
		      (unsigned long)0xad6b5ad6,
		      (unsigned long)0xdef7ddef);
#endif
	return 0;
}
