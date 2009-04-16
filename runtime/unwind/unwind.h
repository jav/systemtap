/* -*- linux-c -*-
 *
 * dwarf unwinder header file
 * Copyright (C) 2008 Red Hat Inc.
 * Copyright (C) 2002-2006 Novell, Inc.
 * 
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_UNWIND_H_
#define _STP_UNWIND_H_

#ifdef STP_USE_DWARF_UNWINDER

#if defined (__x86_64__)
#include "x86_64.h"
#elif  defined (__i386__)
#include "i386.h"
#else
#error "Unsupported dwarf unwind architecture"
#endif

#define STP_MAX_STACK_DEPTH 8

#ifndef BUILD_BUG_ON_ZERO
#define BUILD_BUG_ON_ZERO(e) (sizeof(char[1 - 2 * !!(e)]) - 1)
#endif


#define EXTRA_INFO(f) { \
		BUILD_BUG_ON_ZERO(offsetof(struct unwind_frame_info, f) \
		                  % FIELD_SIZEOF(struct unwind_frame_info, f)) \
		+ offsetof(struct unwind_frame_info, f) \
		  / FIELD_SIZEOF(struct unwind_frame_info, f), \
		FIELD_SIZEOF(struct unwind_frame_info, f) \
	}
#define PTREGS_INFO(f) EXTRA_INFO(regs.f)

static const struct {
	unsigned offs:BITS_PER_LONG / 2;
	unsigned width:BITS_PER_LONG / 2;
} reg_info[] = {
	UNW_REGISTER_INFO
};

#undef PTREGS_INFO
#undef EXTRA_INFO

#ifndef REG_INVALID
#define REG_INVALID(r) (reg_info[r].width == 0)
#endif

#define DW_CFA_nop                          0x00
#define DW_CFA_set_loc                      0x01
#define DW_CFA_advance_loc1                 0x02
#define DW_CFA_advance_loc2                 0x03
#define DW_CFA_advance_loc4                 0x04
#define DW_CFA_offset_extended              0x05
#define DW_CFA_restore_extended             0x06
#define DW_CFA_undefined                    0x07
#define DW_CFA_same_value                   0x08
#define DW_CFA_register                     0x09
#define DW_CFA_remember_state               0x0a
#define DW_CFA_restore_state                0x0b
#define DW_CFA_def_cfa                      0x0c
#define DW_CFA_def_cfa_register             0x0d
#define DW_CFA_def_cfa_offset               0x0e
#define DW_CFA_def_cfa_expression           0x0f
#define DW_CFA_expression                   0x10
#define DW_CFA_offset_extended_sf           0x11
#define DW_CFA_def_cfa_sf                   0x12
#define DW_CFA_def_cfa_offset_sf            0x13
#define DW_CFA_val_offset                   0x14
#define DW_CFA_val_offset_sf                0x15
#define DW_CFA_val_expression               0x16
#define DW_CFA_lo_user                      0x1c
#define DW_CFA_GNU_window_save              0x2d
#define DW_CFA_GNU_args_size                0x2e
#define DW_CFA_GNU_negative_offset_extended 0x2f
#define DW_CFA_hi_user                      0x3f

#define DW_EH_PE_absptr   0x00
#define DW_EH_PE_leb128   0x01
#define DW_EH_PE_data2    0x02
#define DW_EH_PE_data4    0x03
#define DW_EH_PE_data8    0x04
#define DW_EH_PE_FORM     0x07 /* mask */
#define DW_EH_PE_signed   0x08 /* signed versions of above have this bit set */

#define DW_EH_PE_pcrel    0x10
#define DW_EH_PE_textrel  0x20
#define DW_EH_PE_datarel  0x30
#define DW_EH_PE_funcrel  0x40
#define DW_EH_PE_aligned  0x50
#define DW_EH_PE_ADJUST   0x70 /* mask */
#define DW_EH_PE_indirect 0x80
#define DW_EH_PE_omit     0xff

typedef unsigned long uleb128_t;
typedef   signed long sleb128_t;

static struct unwind_table {
	unsigned long pc; /* text */
	unsigned long range; /* text_size */
	const void *address; /* unwind_data */
	unsigned long size; /* unwind_data_len */
	const unsigned char *header; /* unwind_header */
	unsigned long hdrsz;
	struct unwind_table *link;
	const char *name; /* module name */
} root_table;

struct unwind_item {
	enum item_location {
		Nowhere,
		Memory,
		Register,
		Value
	} where;
	uleb128_t value;
};

struct unwind_state {
	uleb128_t loc, org;
	const u8 *cieStart, *cieEnd;
	uleb128_t codeAlign;
	sleb128_t dataAlign;
	struct cfa {
		uleb128_t reg, offs;
	} cfa;
	struct unwind_item regs[ARRAY_SIZE(reg_info)];
	unsigned stackDepth:8;
	unsigned version:8;
	const u8 *label;
	const u8 *stack[STP_MAX_STACK_DEPTH];
};

static const struct cfa badCFA = { ARRAY_SIZE(reg_info), 1 };
static unsigned long read_pointer(const u8 **pLoc,
                                  const void *end,
                                  signed ptrType);
static const u32 bad_cie, not_fde;
static const u32 *cie_for_fde(const u32 *fde, const struct _stp_module *);
static signed fde_pointer_type(const u32 *cie);


#endif /* STP_USE_DWARF_UNWINDER */
#endif /*_STP_UNWIND_H_*/
