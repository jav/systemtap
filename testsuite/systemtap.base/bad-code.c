// Testcase for PR14107. Missing kernel asm CFI.

#include <signal.h>
#include <stdlib.h>

void sigseg(int sig)
{
  exit(0);
}

int
func (void)
{
  int *foo = (void *) 0x1234;
  *foo = 0x12345;
  return 0;
}

int
main (void)
{
  signal(SIGSEGV, sigseg);
  return func ();
}
