/* COVERAGE: getuid16 geteuid16 getgid16 getegid16 setuid16 setresuid16 */
/* COVERAGE: getresuid16 setgid16 setresgid16 getresgid16 setreuid16 setregid16 */
/* COVERAGE: setfsuid16 setfsgid16 */

#ifdef __i386__

/* These are all obsolete 16-bit calls that are still there for compatibility. */

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/syscall.h>

int main ()
{
	uid_t uid, ruid, euid, suid;
	gid_t gid, rgid, egid, sgid;

	uid = syscall(__NR_getuid);
	// getuid16 () = NNNN

	uid = syscall(__NR_geteuid);
	// geteuid16 () = NNNN

	gid = syscall(__NR_getgid);
	// getgid16 () = NNNN

	gid = syscall(__NR_getegid);
	// getegid16 () = NNNN



	syscall(__NR_setuid, 4096);
	// setuid16 (4096) =

	syscall(__NR_setresuid, -1, 4097, -1);
	// setresuid16 (-1, 4097, -1) =

	syscall(__NR_getresuid, &ruid, &euid, &suid);
	// getresuid16 (XXXX, XXXX, XXXX) =

	syscall(__NR_setgid, 4098);
	// setgid16 (4098) =

	syscall(__NR_setresgid, -1, 4099, -1);
	// setresgid16 (-1, 4099, -1) =
	
	syscall(__NR_getresgid, &rgid, &egid, &sgid);
	// getresgid16 (XXXX, XXXX, XXXX) =

	syscall(__NR_setreuid, -1, 5000);
	// setreuid16 (-1, 5000) = 

	syscall(__NR_setreuid, 5001, -1);
	// setreuid16 (5001, -1) = 

	syscall(__NR_setregid, -1, 5002);
	// setregid16 (-1, 5002) = 

	syscall(__NR_setregid, 5003, -1);
	// setregid16 (5003, -1) = 

	syscall(__NR_setfsuid, 5004);
	// setfsuid16 (5004) = 
	
	syscall(__NR_setfsgid, 5005);
	// setfsgid16 (5005) =

	return 0;
}	

#endif /* __i386__ */
