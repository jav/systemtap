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
  // getuid () = NNNN
  
  euid = geteuid();
  // geteuid () = NNNN
  
  rgid = getgid();
  // getgid () = NNNN

  egid = getegid();
  // getegid () = NNNN



  setuid(4096);
  // setuid (4096) = NNNN

  seteuid(4097);
  // setresuid (-1, 4097, -1) = NNNN

  getresuid(&ruid, &euid, &suid);
  // getresuid (XXXX, XXXX, XXXX) = 0

  setgid(4098);
  // setgid (4098) = NNNN

  setegid(4099);
  // setresgid (-1, 4099, -1) = NNNN

  getresgid(&rgid, &egid, &sgid);
  // getresgid (XXXX, XXXX, XXXX) = 0

  setreuid(-1, 5000);
  // setreuid (NNNN, 5000) =

  setreuid(5001, -1);
  // setreuid (5001, NNNN) =

  setregid(-1, 5002);
  // setregid (NNNN, 5002) =

  setregid(5003, -1);
  // setregid (5003, NNNN) =

  setfsuid(5004);
  // setfsuid (5004) = 

  setfsgid(5005);
  // setfsgid (5005) =
  
  return 0;
}	
