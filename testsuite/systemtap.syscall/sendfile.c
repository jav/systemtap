/* COVERAGE: sendfile */
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

int main ()
{
	int fd, read_fd;
	int write_fd;
	struct stat stat_buf;
	off_t offset = 0;
	char buff[512];
	int ret;

	memset(buff, 5, sizeof(buff));
	
	/* create a file with something in it */
        fd = creat("foobar",S_IREAD|S_IWRITE);
	write(fd, buff, sizeof(buff));
	fsync(fd);
	close(fd);
	read_fd = open ("foobar", O_RDONLY);
	if (read_fd < 0) 
		return 1;
 	fstat (read_fd, &stat_buf);
	/* Open the output file for writing */
	write_fd = creat("foobar2",S_IREAD|S_IWRITE|S_IRWXO);

	/* 
	 * For kernel2.6 the write_fd has to be a socket otherwise
	 * sendfile will fail. So we test for failure here.
	 */
	ret = sendfile (write_fd, read_fd, &offset, stat_buf.st_size);
	// sendfile (NNNN, NNNN, XXXX, 512) = -22 (EINVAL)

	close (read_fd);
	close (write_fd);
	unlink("foobar");
	unlink("foobar2");
	
	return 0;
}
