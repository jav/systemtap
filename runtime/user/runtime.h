#ifndef _RUNTIME_H_
#define _RUNTIME_H_
/** @file runtime.h
 * @brief Main include file for runtime functions.
 */

#define __KERNEL__
#include <linux/module.h>
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

#include "print.c"

#endif /* _RUNTIME_H_ */
