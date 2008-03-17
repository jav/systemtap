#include <linux/version.h>
#include <asm/tsc.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
#error "tsc_khz is not exported"
#endif
unsigned int *ptsc = &tsc_khz;
