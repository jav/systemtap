/* COVERAGE: mmap2 munmap msync mlock mlockall munlock munlockall fstat open close */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
	int fd, ret;
	struct stat fs;
	void * r;

	/* create a file with something in it */
	fd = creat("foobar",S_IREAD|S_IWRITE);
	// open ("foobar", O_WRONLY|O_CREAT|O_TRUNC, 0600) = 4
	lseek(fd, 1024, SEEK_SET);
	write(fd, "abcdef", 6);
	close(fd);
	// close (4) = 0

	fd = open("foobar", O_RDONLY);
	// open ("foobar", O_RDONLY) = 4

	/* stat for file size */
	ret = fstat(fd, &fs);
	// fstat (4, XXXX) = 0

	r = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
	// mmap[2]* (XXXX, 1030, PROT_READ, MAP_SHARED, 4, XXXX) = XXXX

	close(fd);

	mlock(r, fs.st_size);
	// mlock (XXXX, 1030) = 0

	msync(r, fs.st_size, MS_SYNC);	
	// msync (XXXX, 1030, MS_SYNC) = 0

	munlock(r, fs.st_size);
	// munlock (XXXX, 1030) = 0

	mlockall(MCL_CURRENT);
	// mlockall (MCL_CURRENT) = 

	munlockall();
	// munlockall () = 0

	munmap(r, fs.st_size);
	// munmap (XXXX, 1030) = 0

	return 0;
}
