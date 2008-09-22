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
	// clone (CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD) = NNNN
	if (!child) {
		int i = 0xfffff;
		while (i > 0) i--;
		exit(0);
	}
	wait4(child, &status, WNOHANG, NULL);
	// wait4 (NNNN, XXXX, WNOHANG, XXXX) = NNNN

	return 0;
}
