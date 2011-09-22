#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>

int main (int argc, char *argv[])
{
  int res;
  struct timespec ts;
  uid_t uid1, uid2;

  /* Give an invalid clockid_t on purpose so the vdso has to call
     through to the kernel syscall. */
  res = clock_gettime(6667, &ts);

  uid1 = syscall(SYS_getuid);
  uid2 = getuid();

  return res + 1 + uid1 - uid2; /* -1 from clock_gettime + 1 == 0 */
}
