#include <linux/compat.h>

void foo(void)
{
    (int) is_compat_task();
}

