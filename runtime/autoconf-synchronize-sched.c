#include <linux/sched.h>

void foo (void)
{
  synchronize_sched ();
}
