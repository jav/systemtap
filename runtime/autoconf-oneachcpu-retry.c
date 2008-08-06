#include <linux/smp.h>

void ____autoconf_func(void)
{
    /* Older on_each_cpu() calls had a "retry" parameter */
    (void)on_each_cpu(NULL, NULL, 0, 0);
}
