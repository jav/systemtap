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

/* Constants for printing address symbols. */

/* Prints address as hex, plus space, no newline. */
#define _STP_SYM_HEXSTR       0
/* Prints symbol when found for address otherwise hex. */
#define _STP_SYM_SYMBOL       1
/* Prints "hex : symbol" when used in combination with _STP_SYM_SYMBOL
   if symbol found for address otherwise hex only. */
#define _STP_SYM_HEX_SYMBOL   2
/* Adds module " [name]" if found. */
#define _STP_SYM_MODULE       4
/* Adds offset to symbol and/or module when used. */
#define _STP_SYM_OFFSET       8
/* Adds size to offset of symbol or module, when _STP_SYM_OFFSET used. */
#define _STP_SYM_SIZE        16
/* Adds the string " (inexact)", if used in together with _STP_SYM_SYMBOL. */
#define _STP_SYM_INEXACT     32
/* Prefixes a space character. */
#define _STP_SYM_PRE_SPACE   64
/* Postfixes a space character, takes precedence over _STP_SYM_NEWLINE. */
#define _STP_SYM_POST_SPACE 128
/* Adds a newline character, doesn't combine with _STP_SYM_POST_SPACE. */
#define _STP_SYM_NEWLINE    256

/* Used for backtraces in hex string form. */
#define _STP_SYM_NONE	(_STP_SYM_HEXSTR | _STP_SYM_POST_SPACE)
/* Special "brief" case, used by print_ubacktrace_brief, no hex if possible. */
#define _STP_SYM_BRIEF	(_STP_SYM_SYMBOL | _STP_SYM_OFFSET | _STP_SYM_NEWLINE)
/* Full symbol format, as used in printed backtraces. */
#define _STP_SYM_FULL	(_STP_SYM_SYMBOL | _STP_SYM_HEX_SYMBOL \
			 | _STP_SYM_MODULE | _STP_SYM_OFFSET \
			 | _STP_SYM_SIZE | _STP_SYM_PRE_SPACE \
			 | _STP_SYM_NEWLINE)
/* Simple symbol format, as used in backtraces for strings. */
#define _STP_SYM_SIMPLE (_STP_SYM_SYMBOL | _STP_SYM_MODULE \
			 | _STP_SYM_OFFSET | _STP_SYM_NEWLINE)
/* All symbol information (as used by [u]symdata). */
#define _STP_SYM_DATA   (_STP_SYM_SYMBOL | _STP_SYM_MODULE \
			 | _STP_SYM_OFFSET | _STP_SYM_SIZE)

struct _stp_symbol {
	unsigned long addr;
	const char *symbol;
};

struct _stp_section {
        const char *name;
        unsigned long static_addr; /* XXX non-null if everywhere the same. */
	unsigned long size; /* length of the address space module covers. */
	struct _stp_symbol *symbols;  /* ordered by address */
  	unsigned num_symbols;

	/* Synthesized index for .debug_frame table, keep section
	   offset to adjust addresses relative to load address. */
	void *debug_hdr;
	uint32_t debug_hdr_len;
	unsigned long sec_load_offset;
};

struct _stp_module {
        const char* name;
        const char* path; /* canonical path used for runtime matching. */
	struct _stp_section *sections;
  	unsigned num_sections;

	/* The .eh_frame unwind data for this module.
	   Note index for .debug_frame (hdr) is per section. */
	void *debug_frame;
	void *eh_frame;
	void *unwind_hdr;	
	uint32_t debug_frame_len;
	uint32_t eh_frame_len;
	uint32_t unwind_hdr_len;
	unsigned long eh_frame_addr; /* Orig load address (offset) .eh_frame */
	unsigned long unwind_hdr_addr; /* same for .eh_frame_hdr */

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

static unsigned long _stp_kmodule_relocate (const char *module,
					    const char *section,
					    unsigned long offset);
static unsigned long _stp_umodule_relocate (const char *module,
					    unsigned long offset,
					    struct task_struct *tsk);
static struct _stp_module *_stp_get_unwind_info (unsigned long addr);

#endif /* _STP_SYM_H_ */
