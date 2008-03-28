/* -*- linux-c -*- 
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_SYM_H_
#define _STP_SYM_H_

#define STP_MODULE_NAME_LEN 64

struct _stp_symbol {
	unsigned long addr;
	const char *symbol;
};

DEFINE_RWLOCK(_stp_module_lock);
#define STP_RLOCK_MODULES  read_lock_irqsave(&_stp_module_lock, flags)
#define STP_WLOCK_MODULES  write_lock_irqsave(&_stp_module_lock, flags)
#define STP_RUNLOCK_MODULES read_unlock_irqrestore(&_stp_module_lock, flags)
#define STP_WUNLOCK_MODULES write_unlock_irqrestore(&_stp_module_lock, flags)

struct _stp_module {
	/* the module name, or "" for kernel */
	char name[STP_MODULE_NAME_LEN];
	
	/* A pointer to the struct module. Note that we cannot */
	/* trust this because as of 2.6.19, there are not yet */
	/* any notifier hooks that will tell us when a module */
	/* is unloading. */
	unsigned long module;
	
	/* the start of the module's text and data sections */
	unsigned long text;
	unsigned long data;
	
	uint32_t text_size;
	
	/* how many symbols this module has that we are interested in */
	uint32_t num_symbols;
	
	/* how many sections this module has */
	uint32_t num_sections;
	
	/* how the data below was allocated */
	/* 0 = kmalloc, 1 = vmalloc */
	struct {
		unsigned symbols :1;
		unsigned symbol_data :1;
		unsigned unwind_data :1;
		unsigned unwind_hdr :1;
	} allocated;
	
	struct _stp_symbol *sections;
	
	/* an array of num_symbols _stp_symbol structs */
	struct _stp_symbol *symbols; /* ordered by address */
	
	/* where we stash our copy of the strtab */
	void *symbol_data;
	
	/* the stack unwind data for this module */
	void *unwind_data;
	void *unwind_hdr;	
	uint32_t unwind_data_len;
	uint32_t unwind_hdr_len;
	uint32_t unwind_is_ehframe; /* unwind data comes from .eh_frame */
	rwlock_t lock; /* lock while unwinding is happening */
	
};

#ifndef STP_MAX_MODULES
#define STP_MAX_MODULES 256
#endif

/* the alphabetical array of modules */
struct _stp_module *_stp_modules[STP_MAX_MODULES];

/* the array of modules ordered by addresses */
struct _stp_module *_stp_modules_by_addr[STP_MAX_MODULES];

/* the number of modules in the arrays */
int _stp_num_modules = 0;
static unsigned long _stp_kretprobe_trampoline = 0;

unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset);
static struct _stp_module *_stp_get_unwind_info (unsigned long addr);
#endif /* _STP_SYM_H_ */
