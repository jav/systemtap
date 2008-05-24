#include <linux/uaccess.h>

void probe_kernel(void *dst, void *src, size_t size)
{
    (void)probe_kernel_read(dst, src, size);
    (void)probe_kernel_write(dst, src, size);
}
