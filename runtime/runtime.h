/* -*- linux-c -*- 
 * main header file
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _RUNTIME_H_
#define _RUNTIME_H_
/** @file runtime.h
 * @brief Main include file for runtime functions.
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>

#ifdef DEBUG
/** Prints debug line.
 * This function prints a debug message immediately to stpd. 
 * If the last character is not a newline, then one is added. 
 * @param args A variable number of args in a format like printf.
 * @ingroup io
 */
static void _stp_dbug (char *func, int line, const char *fmt, ...);
#define dbug(args...) _stp_dbug(__FUNCTION__, __LINE__, args)
#define kbug(args...) {printk("%s:%d ",__FUNCTION__, __LINE__); printk(args); }
#else
#define dbug(args...) ;
#define kbug(args...) ;
#endif /* DEBUG */

/* atomic globals */
static atomic_t _stp_transport_failures = ATOMIC_INIT (0);

#ifdef STP_RELAYFS
static struct
{
	atomic_t ____cacheline_aligned_in_smp seq;
} _stp_seq = { ATOMIC_INIT (0) };

#define _stp_seq_inc() (atomic_inc_return(&_stp_seq.seq))
#endif /* RELAYFS */

#include "print.c"
#include "string.c"
#include "arith.c"
#include "copy.c"
#include "sym.h"


/************* Module Stuff ********************/
static int (*_stp_kta)(unsigned long addr);
static const char * (*_stp_kallsyms_lookup)(unsigned long addr,
					    unsigned long *symbolsize,
					    unsigned long *offset,
					    char **modname, char *namebuf);

/* TEST_MODE is always defined by systemtap */
#ifdef TEST_MODE
#define SYSTEMTAP 1
#endif

#ifdef SYSTEMTAP
/* This implementation is used if stap_[num_]symbols are available. */
static const char * _stp_kallsyms_lookup_tabled (unsigned long addr,
						 unsigned long *symbolsize,
						 unsigned long *offset,
						 char **modname,
						 char *namebuf)
{
  unsigned begin = 0;
  unsigned end = stap_num_symbols;
  /*const*/ struct stap_symbol* s;

  /* binary search on index [begin,end) */
  do
    {
      unsigned mid = (begin + end) / 2;
      if (addr < stap_symbols[mid].addr)
	end = mid;
      else
	begin = mid;
    } while (begin + 1 < end);
  /* result index in $begin, guaranteed between [0,stap_num_symbols) */

  s = & stap_symbols [begin];
  if (addr < s->addr)
    return NULL;
  else
    {
      if (offset) *offset = addr - s->addr;
      if (modname) *modname = (char *) s->modname;
      if (symbolsize)
	{
	  if ((begin + 1) < stap_num_symbols)
	    *symbolsize = stap_symbols[begin+1].addr - s->addr;
	  else
	    *symbolsize = 0;
	  // NB: This is only a heuristic.  Sometimes there are large
	  // gaps between text areas of modules.
	}
      if (namebuf)
	{
	  strlcpy (namebuf, s->symbol, KSYM_NAME_LEN+1);
	  return namebuf;
	}
      else
	return s->symbol;
    }
}
#endif

#ifdef __ia64__	
  struct fnptr func_entry, *pfunc_entry;
#endif
int init_module (void)
{
  _stp_kta = (int (*)(unsigned long))kallsyms_lookup_name("__kernel_text_address");

#ifdef SYSTEMTAP
  if (stap_num_symbols > 0)
    _stp_kallsyms_lookup = & _stp_kallsyms_lookup_tabled;
  else
#endif
#ifdef __ia64__
  {
        func_entry.gp = ((struct fnptr *) kallsyms_lookup_name)->gp;
        func_entry.ip = kallsyms_lookup_name("kallsyms_lookup");
        _stp_kallsyms_lookup = (const char * (*)(unsigned long,unsigned long *,unsigned long *,char **,char *))&func_entry;

  }
#else
    _stp_kallsyms_lookup = (const char * (*)(unsigned long,unsigned long *,unsigned long *,char **,char *))
      kallsyms_lookup_name("kallsyms_lookup");
#endif

  return _stp_transport_init();
}

int probe_start(void);

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

#endif /* _RUNTIME_H_ */
