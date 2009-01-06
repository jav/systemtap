#include <linux/sched.h>

struct task_struct *foo;
void bar () { foo->uid = 0; }
/* as opposed to linux/cred.h wrappers current_uid() etc. */
