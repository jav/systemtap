#ifndef _RUNTIME_H_
#define _RUNTIME_H_
/** @file runtime.h
 * @brief Main include file for runtime functions.
 */

#define __KERNEL__
#include <linux/module.h>
#undef CONFIG_NR_CPUS
#undef CONFIG_SMP
#define CONFIG_NR_CPUS 8
#define CONFIG_SMP
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/percpu.h>

#ifdef DEBUG
#define dbug(args...) \
  {                                             \
    printf("%s:%d: ", __FUNCTION__, __LINE__);  \
    printf(args);                               \
  }
#else
#define dbug(args...) ;
#endif

#include "emul.h"

#undef memcpy
#define memcpy __builtin_memcpy

#define NEED_STAT_LOCKS 0
#define NEED_COUNTER_LOCKS 0

#include "print.c"
#include "string.c"

#endif /* _RUNTIME_H_ */
