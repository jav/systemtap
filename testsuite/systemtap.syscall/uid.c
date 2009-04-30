/* COVERAGE: getuid geteuid getgid getegid setuid setresuid getresuid setgid */
/* COVERAGE: setresgid getresgid setreuid setregid setfsuid setfsgid */
#define _GNU_SOURCE
#include <sys/types.h>
#include <unistd.h>
#include <sys/fsuid.h>

int main ()
{
  uid_t ruid, euid, suid;
  gid_t rgid, egid, sgid;

  ruid = getuid();
  //staptest// getuid () = NNNN
  
  euid = geteuid();
  //staptest// geteuid () = NNNN
  
  rgid = getgid();
  //staptest// getgid () = NNNN

  egid = getegid();
  //staptest// getegid () = NNNN



  setuid(4096);
  //staptest// setuid (4096) = NNNN

  seteuid(4097);
  //staptest// setresuid (-1, 4097, -1) = NNNN

  getresuid(&ruid, &euid, &suid);
  //staptest// getresuid (XXXX, XXXX, XXXX) = 0

  setgid(4098);
  //staptest// setgid (4098) = NNNN

  setegid(4099);
  //staptest// setresgid (-1, 4099, -1) = NNNN

  getresgid(&rgid, &egid, &sgid);
  //staptest// getresgid (XXXX, XXXX, XXXX) = 0

  setreuid(-1, 5000);
  //staptest// setreuid (NNNN, 5000) =

  setreuid(5001, -1);
  //staptest// setreuid (5001, NNNN) =

  setregid(-1, 5002);
  //staptest// setregid (NNNN, 5002) =

  setregid(5003, -1);
  //staptest// setregid (5003, NNNN) =

  setfsuid(5004);
  //staptest// setfsuid (5004) = 

  setfsgid(5005);
  //staptest// setfsgid (5005) =
  
  return 0;
}	
