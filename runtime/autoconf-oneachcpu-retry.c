#include <linux/stddef.h>
#include <linux/irqflags.h>
#include <linux/smp.h>

static void no_op(void *arg)
{
}

void ____autoconf_func(void)
{
    /* Older on_each_cpu() calls had a "retry" parameter */
    (void)on_each_cpu(no_op, NULL, 0, 0);
}
