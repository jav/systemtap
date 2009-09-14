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
	unsigned long size; /* length of the address space module covers. */
	struct _stp_symbol *symbols;  /* ordered by address */
  	unsigned num_symbols;
};


struct _stp_module {
        const char* name;
        const char* path; /* canonical path used for runtime matching. */
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
	void *debug_frame;
	void *eh_frame;
	void *unwind_hdr;	
	uint32_t debug_frame_len;
	uint32_t eh_frame_len;
	uint32_t unwind_hdr_len;
	unsigned long eh_frame_addr; /* Orig load address (offset) .eh_frame */
	/* build-id information */
	unsigned char *build_id_bits;
	unsigned long  build_id_offset;
	unsigned long  notes_sect;
	int build_id_len;
};


/* Defined by translator-generated stap-symbols.h. */
static struct _stp_module *_stp_modules [];
static unsigned _stp_num_modules;

/* Used in the unwinder to special case unwinding through kretprobes. */
/* Initialized through translator (stap-symbols.h) relative to kernel */
/* load address, fixup by transport symbols _stp_do_relocation */
static unsigned long _stp_kretprobe_trampoline;

static unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset);
static struct _stp_module *_stp_get_unwind_info (unsigned long addr);

#endif /* _STP_SYM_H_ */
