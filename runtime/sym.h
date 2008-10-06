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

struct _stp_symbol {
	unsigned long addr;
	const char *symbol;
};

struct _stp_section {
        const char *name;
        unsigned long addr; /* XXX: belongs in per-address-space tables */
	struct _stp_symbol *symbols;  /* ordered by address */
  	unsigned num_symbols;
};


struct _stp_module {
        const char* name;
	struct _stp_section *sections;
  	unsigned num_sections;

	/* A pointer to the struct module. Note that we cannot */
	/* trust this because as of 2.6.19, there are not yet */
	/* any notifier hooks that will tell us when a module */
	/* is unloading. */
  	unsigned long module; /* XXX: why not struct module * ? */

	 /* This is to undo .debug_frame relocation performed by elfutils, */
	 /* which is done during the translate phase when we encode the    */
	 /* unwind data into the module. See adjustStartLoc() in unwind.c. */
  	unsigned long dwarf_module_base;

	/* the stack unwind data for this module */
	void *unwind_data;
	void *unwind_hdr;	
	uint32_t unwind_data_len;
	uint32_t unwind_hdr_len;
	uint32_t unwind_is_ehframe; /* unwind data comes from .eh_frame */
	/* build-id information */
	unsigned char *build_id_bits;
	unsigned long  build_id_offset;
	unsigned long  notes_sect; /* kernel: 1 - no build-id 
				    *  	      2 - has build-id 
				    * module: 0 - unloaded
				    *	      1 - loaded and no build-id
				    *	      Other - note section address  
				   */
	int build_id_len;
};


/* Defined by translator-generated stap-symbols.h. */
struct _stp_module *_stp_modules [];
unsigned _stp_num_modules;


/* the number of modules in the arrays */

static unsigned long _stp_kretprobe_trampoline = 0;

unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset);
static struct _stp_module *_stp_get_unwind_info (unsigned long addr);

#endif /* _STP_SYM_H_ */
