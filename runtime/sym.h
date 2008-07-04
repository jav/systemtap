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

struct _stp_module {
	/* the module name, or "" for kernel */
	char name[STP_MODULE_NAME_LEN];
	
	/* A pointer to the struct module. Note that we cannot */
	/* trust this because as of 2.6.19, there are not yet */
	/* any notifier hooks that will tell us when a module */
	/* is unloading. */
  	unsigned long module; /* XXX: why not struct module * ? */
	
	struct _stp_symbol *sections;
  	unsigned num_sections;
	struct _stp_symbol *symbols;  /* ordered by address */
  	unsigned num_symbols;

	/* the stack unwind data for this module */
	void *unwind_data;
	void *unwind_hdr;	
	uint32_t unwind_data_len;
	uint32_t unwind_hdr_len;
	uint32_t unwind_is_ehframe; /* unwind data comes from .eh_frame */
};


/* Defined by translator-generated stap-symbols.h. */
struct _stp_module *_stp_modules [];
int _stp_num_modules;


#if 0
/* the array of modules ordered by addresses */
struct _stp_module *_stp_modules_by_addr[STP_MAX_MODULES];
#endif

/* the number of modules in the arrays */

static unsigned long _stp_kretprobe_trampoline = 0;

unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset);
static struct _stp_module *_stp_get_unwind_info (unsigned long addr);
#endif /* _STP_SYM_H_ */
