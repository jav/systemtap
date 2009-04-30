/* COVERAGE: fork wait4 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

int main ()
{
	pid_t child;
	int status;
	
	child = fork();
	//staptest// fork () = NNNN
	if (!child) {
		int i = 0xfffff;
		while (i > 0) i--;
		exit(0);
	}
	wait4(child, &status, WNOHANG, NULL);
	//staptest// wait4 (NNNN, XXXX, WNOHANG, XXXX) = NNNN

	return 0;
}
