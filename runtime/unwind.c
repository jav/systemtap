/* -*- linux-c -*-
 * kernel stack unwinding
 * Copyright (C) 2008-2010 Red Hat Inc.
 *
 * Based on old kernel code that is
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 *
 * This code is released under version 2 of the GNU GPL.
 *
 * This code currently does stack unwinding in the kernel and modules.
 * It has been extended to handle userspace unwinding using systemtap
 * data structures.
 */

#include "unwind/unwind.h"

#ifdef STP_USE_DWARF_UNWINDER

struct unwind_context {
    struct unwind_frame_info info;
    struct unwind_state state;
};

struct eh_frame_hdr_table_entry {
	unsigned long start, fde;
};

static int cmp_eh_frame_hdr_table_entries(const void *p1, const void *p2)
{
	const struct eh_frame_hdr_table_entry *e1 = p1;
	const struct eh_frame_hdr_table_entry *e2 = p2;
	return (e1->start > e2->start) - (e1->start < e2->start);
}

static void swap_eh_frame_hdr_table_entries(void *p1, void *p2, int size)
{
	struct eh_frame_hdr_table_entry *e1 = p1;
	struct eh_frame_hdr_table_entry *e2 = p2;
	unsigned long v;

	v = e1->start;
	e1->start = e2->start;
	e2->start = v;
	v = e1->fde;
	e1->fde = e2->fde;
	e2->fde = v;
}

static uleb128_t get_uleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	uleb128_t value = 0;
	unsigned shift;

	for (shift = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (uleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur++ & 0x80))
			break;
	}
	*pcur = cur;

	return value;
}

static sleb128_t get_sleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	sleb128_t value = 0;
	unsigned shift;

	for (shift = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (sleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur & 0x80)) {
			value |= -(*cur++ & 0x40) << shift;
			break;
		}
	}
	*pcur = cur;

	return value;
}

/* given an FDE, find its CIE */
static const u32 *cie_for_fde(const u32 *fde, void *unwind_data,
			      uint32_t table_len, int is_ehframe)
{
	const u32 *cie;

	/* check that length is proper */
	if (!*fde || (*fde & (sizeof(*fde) - 1)))
		return &bad_cie;

	/* CIE id for eh_frame is 0, otherwise 0xffffffff */
	if (is_ehframe && fde[1] == 0)
		return &not_fde;
	else if (fde[1] == 0xffffffff)
		return &not_fde;

	/* OK, must be an FDE.  Now find its CIE. */

	/* CIE_pointer must be a proper offset */
	if ((fde[1] & (sizeof(*fde) - 1)) || fde[1] > (unsigned long)(fde + 1) - (unsigned long)unwind_data) {
		_stp_warn("invalid fde[1]=%lx fde+1=%lx, unwind_data=%lx  %lx\n",
			    (unsigned long)fde[1], (unsigned long)(fde + 1),
			    (unsigned long)unwind_data, (unsigned long)(fde + 1) - (unsigned long)unwind_data);
		return NULL;	/* this is not a valid FDE */
	}

	/* cie pointer field is different in eh_frame vs debug_frame */
	if (is_ehframe)
		cie = fde + 1 - fde[1] / sizeof(*fde);
	else
		cie = unwind_data + fde[1];

	/* Make sure address falls in the table */
	if (((void *)cie) < ((void*)unwind_data)
	    || ((void*)cie) > ((void*)(unwind_data + table_len)))
	  return NULL;

	if (*cie <= sizeof(*cie) + 4 || *cie >= fde[1] - sizeof(*fde)
	    || (*cie & (sizeof(*cie) - 1))
	    || (cie[1] != 0xffffffff && cie[1] != 0)) {
		_stp_warn("cie is not valid %lx %x %x %x\n", (unsigned long)cie, *cie, fde[1], cie[1]);
		return NULL;	/* this is not a (valid) CIE */
	}

	return cie;
}

/* read an encoded pointer and increment *pLoc past the end of the
 * data read. */
static unsigned long read_ptr_sect(const u8 **pLoc, const void *end,
				   signed ptrType, unsigned long textAddr,
				   unsigned long dataAddr)
{
	unsigned long value = 0;
	union {
		const u8 *p8;
		const u16 *p16u;
		const s16 *p16s;
		const u32 *p32u;
		const s32 *p32s;
		const unsigned long *pul;
	} ptr;

	if (ptrType < 0 || ptrType == DW_EH_PE_omit)
		return 0;

	ptr.p8 = *pLoc;
	switch (ptrType & DW_EH_PE_FORM) {
	case DW_EH_PE_data2:
		if (end < (const void *)(ptr.p16u + 1))
			return 0;
		if (ptrType & DW_EH_PE_signed)
			value = _stp_get_unaligned(ptr.p16s++);
		else
			value = _stp_get_unaligned(ptr.p16u++);
		break;
	case DW_EH_PE_data4:
#ifdef CONFIG_64BIT
		if (end < (const void *)(ptr.p32u + 1))
			return 0;
		if (ptrType & DW_EH_PE_signed)
			value = _stp_get_unaligned(ptr.p32s++);
		else
			value = _stp_get_unaligned(ptr.p32u++);
		break;
	case DW_EH_PE_data8:
		BUILD_BUG_ON(sizeof(u64) != sizeof(value));
#else
		BUILD_BUG_ON(sizeof(u32) != sizeof(value));
#endif
	case DW_EH_PE_absptr:
		if (end < (const void *)(ptr.pul + 1))
			return 0;
		value = _stp_get_unaligned(ptr.pul++);
		break;
	case DW_EH_PE_leb128:
		BUILD_BUG_ON(sizeof(uleb128_t) > sizeof(value));
		value = ptrType & DW_EH_PE_signed ? get_sleb128(&ptr.p8, end)
		    : get_uleb128(&ptr.p8, end);
		if ((const void *)ptr.p8 > end)
			return 0;
		break;
	default:
		return 0;
	}
	switch (ptrType & DW_EH_PE_ADJUST) {
	case DW_EH_PE_absptr:
		break;
	case DW_EH_PE_pcrel:
		value += (unsigned long)*pLoc;
		break;
	case DW_EH_PE_textrel:
		value += textAddr;
		break;
	case DW_EH_PE_datarel:
		value += dataAddr;
		break;
	default:
		return 0;
	}
	if ((ptrType & DW_EH_PE_indirect)
	    && _stp_read_address(value, (unsigned long *)value, KERNEL_DS))
		return 0;
	*pLoc = ptr.p8;

	return value;
}

static unsigned long read_pointer(const u8 **pLoc, const void *end, signed ptrType)
{
	return read_ptr_sect(pLoc, end, ptrType, 0, 0);
}

static signed fde_pointer_type(const u32 *cie, void *unwind_data,
			       uint32_t table_len)
{
	const u8 *ptr = (const u8 *)(cie + 2);
	unsigned version = *ptr;

	if (version != 1 && version != 3 && version != 4)
		return -1;	/* unsupported */
	if (*++ptr) {
		const char *aug;
		const u8 *end = (const u8 *)(cie + 1) + *cie;
		uleb128_t len;

		/* end of cie should fall within unwind table. */
		if (((void*)end) < ((void *)unwind_data)
		    || ((void *)end) > ((void *)(unwind_data + table_len)))
		  return -1;

		/* check if augmentation size is first (and thus present) */
		if (*ptr != 'z')
			return -1;
		/* check if augmentation string is nul-terminated */
		if ((ptr = memchr(aug = (const void *)ptr, 0, end - ptr)) == NULL)
			return -1;
		++ptr;		/* skip terminator */
		get_uleb128(&ptr, end);	/* skip code alignment */
		get_sleb128(&ptr, end);	/* skip data alignment */
		/* skip return address column */
		version <= 1 ? (void)++ptr : (void)get_uleb128(&ptr, end);
		len = get_uleb128(&ptr, end);	/* augmentation length */
		if (ptr + len < ptr || ptr + len > end)
			return -1;
		end = ptr + len;
		while (*++aug) {
			if (ptr >= end)
				return -1;
			switch (*aug) {
			case 'L':
				++ptr;
				break;
			case 'P':{
					signed ptrType = *ptr++;

					if (!read_pointer(&ptr, end, ptrType) || ptr > end)
						return -1;
				}
				break;
			case 'R':
				return *ptr;
			default:
				return -1;
			}
		}
	}
	return DW_EH_PE_absptr;
}

static int advance_loc(unsigned long delta, struct unwind_state *state)
{
	state->loc += delta * state->codeAlign;
	dbug_unwind(1, "state->loc=%lx\n", state->loc);
	return delta > 0;
}

static void set_rule(uleb128_t reg, enum item_location where, uleb128_t value, struct unwind_state *state)
{
	dbug_unwind(1, "reg=%lx, where=%d, value=%lx\n", reg, where, value);
	if (reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].value = value;
	}
}

static void set_expr_rule(uleb128_t reg, enum item_location where,
			  const u8 **expr, const u8 *end,
			  struct unwind_state *state)
{
	const u8 *const start = *expr;
	uleb128_t len = get_uleb128(expr, end);
	dbug_unwind(1, "reg=%lx, where=%d, expr=%lu@%p\n",
		    reg, where, len, *expr);
	if (end - *expr >= len && reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].expr = start;
		*expr += len;
	}
}

/* Limit the number of instructions we process. Arbitrary limit.
   512 should be enough for anybody... */
#define MAX_CFI 512

static int processCFI(const u8 *start, const u8 *end, unsigned long targetLoc, signed ptrType, struct unwind_state *state)
{
	union {
		const u8 *p8;
		const u16 *p16;
		const u32 *p32;
	} ptr;
	int result = 1;

	if (end - start > MAX_CFI)
	  return 0;

	dbug_unwind(1, "targetLoc=%lx state->loc=%lx\n", targetLoc, state->loc);
	if (start != state->cieStart) {
		state->loc = state->org;
		result = processCFI(state->cieStart, state->cieEnd, 0, ptrType, state);
		if (targetLoc == 0 && state->label == NULL)
			return result;
	}

	for (ptr.p8 = start; result && ptr.p8 < end;) {
		switch (*ptr.p8 >> 6) {
			uleb128_t value;
		case 0:
			switch (*ptr.p8++) {
			case DW_CFA_nop:
				dbug_unwind(1, "DW_CFA_nop\n");
				break;
			case DW_CFA_set_loc:
				if ((state->loc = read_pointer(&ptr.p8, end, ptrType)) == 0)
					result = 0;
				dbug_unwind(1, "DW_CFA_set_loc %lx (result=%d)\n", state->loc, result);
				break;
			case DW_CFA_advance_loc1:
				result = ptr.p8 < end && advance_loc(*ptr.p8++, state);
				dbug_unwind(1, "DW_CFA_advance_loc1 %d\n", result);
				break;
			case DW_CFA_advance_loc2:
				result = ptr.p8 <= end + 2 && advance_loc(*ptr.p16++, state);
				dbug_unwind(1, "DW_CFA_advance_loc2 %d\n", result);
				break;
			case DW_CFA_advance_loc4:
				result = ptr.p8 <= end + 4 && advance_loc(*ptr.p32++, state);
				dbug_unwind(1, "DW_CFA_advance_loc4 %d\n", result);
				break;
			case DW_CFA_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, get_uleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_offset_extended\n");
				break;
			case DW_CFA_val_offset:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value, get_uleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_val_offset\n");
				break;
			case DW_CFA_offset_extended_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, get_sleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_offset_extended_sf\n");
				break;
			case DW_CFA_val_offset_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value, get_sleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_val_offset_sf\n");
				break;
			case DW_CFA_restore_extended:
			case DW_CFA_undefined:
			case DW_CFA_same_value:
				set_rule(get_uleb128(&ptr.p8, end), Nowhere, 0, state);
				dbug_unwind(1, "DW_CFA_undefined\n");
				break;
			case DW_CFA_register:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Register, get_uleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_register\n");
				break;
			case DW_CFA_expression:
				value = get_uleb128(&ptr.p8, end);
				set_expr_rule(value, Expr, &ptr.p8, end, state);
				dbug_unwind(1, "DW_CFA_expression\n");
				break;
			case DW_CFA_val_expression:
				value = get_uleb128(&ptr.p8, end);
				set_expr_rule(value, ValExpr, &ptr.p8, end,
					      state);
				dbug_unwind(1, "DW_CFA_val_expression\n");
				break;
			case DW_CFA_remember_state:
				dbug_unwind(1, "DW_CFA_remember_state\n");
				if (ptr.p8 == state->label) {
					state->label = NULL;
					return 1;
				}
				if (state->stackDepth >= STP_MAX_STACK_DEPTH)
					return 0;
				state->stack[state->stackDepth++] = ptr.p8;
				break;
			case DW_CFA_restore_state:
				dbug_unwind(1, "DW_CFA_restore_state\n");
				if (state->stackDepth) {
					const uleb128_t loc = state->loc;
					const u8 *label = state->label;

					state->label = state->stack[state->stackDepth - 1];
					state->cfa_is_expr = 0;
					memcpy(&state->cfa, &badCFA, sizeof(state->cfa));
					memset(state->regs, 0, sizeof(state->regs));
					state->stackDepth = 0;
					result = processCFI(start, end, 0, ptrType, state);
					state->loc = loc;
					state->label = label;
				} else
					return 0;
				break;
			case DW_CFA_def_cfa:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				dbug_unwind(1, "DW_CFA_def_cfa reg=%ld\n", state->cfa.reg);
				/*nobreak */
			case DW_CFA_def_cfa_offset:
				state->cfa.offs = get_uleb128(&ptr.p8, end);
				dbug_unwind(1, "DW_CFA_def_cfa_offset offs=%lx\n", state->cfa.offs);
				break;
			case DW_CFA_def_cfa_sf:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				dbug_unwind(1, "DW_CFA_def_cfa_sf reg=%ld\n", state->cfa.reg);
				/*nobreak */
			case DW_CFA_def_cfa_offset_sf:
				state->cfa.offs = get_sleb128(&ptr.p8, end) * state->dataAlign;
				dbug_unwind(1, "DW_CFA_def_cfa_offset_sf offs=%lx\n", state->cfa.offs);
				break;
			case DW_CFA_def_cfa_register:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				dbug_unwind(1, "DW_CFA_def_cfa_register reg=%ld\n", state->cfa.reg);
				break;
			case DW_CFA_def_cfa_expression: {
				const u8 *cfa_expr = ptr.p8;
				value = get_uleb128(&ptr.p8, end);
				if (ptr.p8 < end && end - ptr.p8 >= value) {
					state->cfa_is_expr = 1;
					state->cfa_expr = cfa_expr;
					ptr.p8 += value;
					dbug_unwind(1, "DW_CFA_def_cfa_expression %lu@%p\n", value, cfa_expr);
				}
				else
					_stp_warn("BAD DW_CFA_def_cfa_expression value %lu\n", value);
				break;
			}
			case DW_CFA_GNU_args_size:
				get_uleb128(&ptr.p8, end);
				dbug_unwind(1, "DW_CFA_GNU_args_size\n");
				break;
			case DW_CFA_GNU_negative_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, (uleb128_t)0 - get_uleb128(&ptr.p8, end), state);
				dbug_unwind(1, "DW_CFA_GNU_negative_offset_extended\n");
				break;
			case DW_CFA_GNU_window_save:
			default:
				_stp_warn("unimplemented call frame instruction: 0x%x\n", *(ptr.p8 - 1));
				result = 0;
				break;
			}
			break;
		case 1:
			result = advance_loc(*ptr.p8++ & 0x3f, state);
			dbug_unwind(1, "case 1\n");
			break;
		case 2:
			value = *ptr.p8++ & 0x3f;
			set_rule(value, Memory, get_uleb128(&ptr.p8, end), state);
			dbug_unwind(1, "case 2\n");
			break;
		case 3:
			set_rule(*ptr.p8++ & 0x3f, Nowhere, 0, state);
			dbug_unwind(1, "case 3\n");
			break;
		}
		dbug_unwind(1, "targetLoc=%lx state->loc=%lx\n", targetLoc, state->loc);
		if (ptr.p8 > end)
			result = 0;
		if (result && targetLoc != 0 && targetLoc < state->loc)
			return 1;
	}
	return result && ptr.p8 == end && (targetLoc == 0 || state->label == NULL);
}

#ifdef DEBUG_UNWIND
static const char *_stp_enc_hi_name[] = {
	"DW_EH_PE",
	"DW_EH_PE_pcrel",
	"DW_EH_PE_textrel",
	"DW_EH_PE_datarel",
	"DW_EH_PE_funcrel",
	"DW_EH_PE_aligned"
};
static const char *_stp_enc_lo_name[] = {
	"_absptr",
	"_uleb128",
	"_udata2",
	"_udata4",
	"_udata8",
	"_sleb128",
	"_sdata2",
	"_sdata4",
	"_sdata8"
};
static char *_stp_eh_enc_name(signed type)
{
	static char buf[64];
	int hi, low;
	if (type == DW_EH_PE_omit)
		return "DW_EH_PE_omit";

	hi = (type & DW_EH_PE_ADJUST) >> 4;
	low = type & DW_EH_PE_FORM;
	if (hi > 5 || low > 4 || (low == 0 && (type & DW_EH_PE_signed))) {
	    snprintf(buf, sizeof(buf), "ERROR:encoding=0x%x", type);
		return buf;
	}

	buf[0] = 0;
	if (type & DW_EH_PE_indirect)
		strlcpy(buf, "DW_EH_PE_indirect|", sizeof(buf));
	strlcat(buf, _stp_enc_hi_name[hi], sizeof(buf));

	if (type & DW_EH_PE_signed)
		low += 4;
	strlcat(buf, _stp_enc_lo_name[low], sizeof(buf));
	return buf;
}
#endif /* DEBUG_UNWIND */

// If this is an address inside a module, adjust for section relocation
// and the elfutils base relocation done during loading of the .dwarf_frame
// in translate.cxx.
static unsigned long
adjustStartLoc (unsigned long startLoc, struct task_struct *tsk,
		struct _stp_module *m,
		struct _stp_section *s,
		unsigned ptrType, int is_ehframe)
{
  unsigned long vm_addr = 0;

  /* XXX - some, or all, of this should really be done by
     _stp_module_relocate and/or read_pointer. */
  dbug_unwind(2, "adjustStartLoc=%lx, ptrType=%s, m=%s, s=%s eh=%d\n",
	      startLoc, _stp_eh_enc_name(ptrType), m->name, s->name, is_ehframe);
  if (startLoc == 0
      || strcmp (m->name, "kernel")  == 0
      || (strcmp (s->name, ".absolute") == 0 && !is_ehframe))
    return startLoc;

  /* eh_frame data has been loaded in the kernel, so readjust offset. */
  if (is_ehframe) {
    dbug_unwind(2, "eh_frame=%lx, eh_frame_addr=%lx\n", (unsigned long) m->eh_frame, m->eh_frame_addr);
    if ((ptrType & DW_EH_PE_ADJUST) == DW_EH_PE_pcrel) {
      startLoc -= (unsigned long) m->eh_frame;
      startLoc += m->eh_frame_addr;
    }
    /* User space exec */
    if (strcmp (s->name, ".absolute") == 0)
      return startLoc;
  }

  /* User space or kernel dynamic module. */
  if (strcmp (s->name, ".dynamic") == 0)
    stap_find_vma_map_info_user(tsk->group_leader, m, &vm_addr, NULL, NULL);
  else
    vm_addr = s->static_addr;

  if (is_ehframe)
    return startLoc + vm_addr;
  else
    return startLoc + vm_addr - s->sec_load_offset;
}

/* If we previously created an unwind header, then use it now to binary search */
/* for the FDE corresponding to pc. */
static u32 *_stp_search_unwind_hdr(unsigned long pc, struct task_struct *tsk,
				   struct _stp_module *m,
				   struct _stp_section *s,
				   int is_ehframe)
{
	const u8 *ptr, *end, *hdr = is_ehframe ? m->unwind_hdr: s->debug_hdr;
	unsigned long startLoc;
	u32 *fde = NULL;
	unsigned num, tableSize, t2;
	unsigned long eh_hdr_addr = m->unwind_hdr_addr;

	if (hdr == NULL || hdr[0] != 1)
		return NULL;

	dbug_unwind(1, "search for %lx", pc);

	/* table_enc */
	switch (hdr[3] & DW_EH_PE_FORM) {
	case DW_EH_PE_absptr:
		tableSize = sizeof(unsigned long);
		break;
	case DW_EH_PE_data2:
		tableSize = 2;
		break;
	case DW_EH_PE_data4:
		tableSize = 4;
		break;
	case DW_EH_PE_data8:
		tableSize = 8;
		break;
	default:
		_stp_warn("bad unwind table encoding");
		return NULL;
	}
	ptr = hdr + 4;
	end = hdr + (is_ehframe ? m->unwind_hdr_len : s->debug_hdr_len);
	{
		// XXX Can the header validity be checked just once?
		unsigned long eh = read_ptr_sect(&ptr, end, hdr[1], 0,
						 eh_hdr_addr);
		if ((hdr[1] & DW_EH_PE_ADJUST) == DW_EH_PE_pcrel)
			eh = eh - (unsigned long)hdr + eh_hdr_addr;
		if ((is_ehframe && eh != (unsigned long)m->eh_frame_addr)) {
			_stp_warn("eh_frame_ptr in eh_frame_hdr 0x%lx not valid; eh_frame_addr = 0x%lx", eh, (unsigned long)m->eh_frame_addr);
			return NULL;
		}
	}
	num = read_ptr_sect(&ptr, end, hdr[2], 0, eh_hdr_addr);
	if (num == 0 || num != (end - ptr) / (2 * tableSize)
	    || (end - ptr) % (2 * tableSize)) {
		_stp_warn("unwind Bad num=%d end-ptr=%ld 2*tableSize=%d",
			    num, (long)(end - ptr), 2 * tableSize);
		return NULL;
	}

	do {
		const u8 *cur = ptr + (num / 2) * (2 * tableSize);
		startLoc = read_ptr_sect(&cur, cur + tableSize, hdr[3], 0,
					 eh_hdr_addr);
		startLoc = adjustStartLoc(startLoc, tsk, m, s, hdr[3],
					  is_ehframe);
		if (pc < startLoc)
			num /= 2;
		else {
			ptr = cur - tableSize;
			num = (num + 1) / 2;
		}
	} while (startLoc && num > 1);

	if (num == 1
	    && (startLoc = adjustStartLoc(read_ptr_sect(&ptr, ptr + tableSize, hdr[3], 0, eh_hdr_addr), tsk, m, s, hdr[3], is_ehframe)) != 0 && pc >= startLoc) {
		unsigned long off;
		off = read_ptr_sect(&ptr, ptr + tableSize, hdr[3],
				    0, eh_hdr_addr);
		dbug_unwind(1, "fde off=%lx\n", off);
		/* For real eh_frame_hdr the actual fde address is at the
		   new eh_frame load address. For our own debug_hdr created
		   table the fde is an offset into the debug_frame table. */
		if (is_ehframe)
			fde = off - m->eh_frame_addr + m->eh_frame;
		else
			fde = m->debug_frame + off;
	}

	dbug_unwind(1, "returning fde=%lx startLoc=%lx", (unsigned long) fde, startLoc);
	return fde;
}

#define FRAME_REG(r, t) (((t *)frame)[reg_info[r].offs])

#ifndef CONFIG_64BIT
# define CASES CASE(8); CASE(16); CASE(32)
#else
# define CASES CASE(8); CASE(16); CASE(32); CASE(64)
#endif

#define MAX_EXPR_STACK	8	/* arbitrary */

static int compute_expr(const u8 *expr, struct unwind_frame_info *frame,
			unsigned long *result)
{
	/*
	 * We previously validated the length, so we won't read off the end.
	 */
	uleb128_t len = get_uleb128(&expr, (const u8 *) -1UL);
	const u8 *const start = expr;
	const u8 *const end = expr + len;

	long stack[MAX_EXPR_STACK]; /* stack slots are signed */
	unsigned int sp = 0;
#define PUSH(val) do { \
		if (sp == MAX_EXPR_STACK) \
			goto overflow; \
		stack[sp++] = (val); \
	} while (0)
#define POP ({ \
		if (sp == 0) \
			goto underflow; \
		stack[--sp]; \
	})
#define NEED(n)	do { \
		if (end - expr < (n)) \
			goto truncated; \
	} while (0)

	while (expr < end) {
		uleb128_t value;
		union {
			u8 u8;
			s8 s8;
			u16 u16;
			s16 s16;
			u32 u32;
			s32 s32;
			u64 u64;
			s64 s64;
		} u;
		const u8 op = *expr++;
		dbug_unwind(3, " expr op 0x%x (%ld left)\n", op, (long)(end - expr));
		switch (op) {
		case DW_OP_nop:
			break;

		case DW_OP_bra:
			if (POP == 0)
				break;
			/* Fall through.  */
		case DW_OP_skip:
			NEED(sizeof(u.s16));
			memcpy(&u.s16, expr, sizeof(u.s16));
			expr += sizeof(u.s16);
			if (u.s16 < 0 ?
			    unlikely(expr - start < -u.s16) :
			    unlikely(end - expr < u.s16)) {
				_stp_warn("invalid skip %d in CFI expression\n", (int) u.s16);
				return 1;
			}
			/*
			 * A backward branch could lead to an infinite loop.
			 * So punt it until we find we actually need it.
			 */
			if (u.s16 < 0) {
				_stp_warn("backward branch in CFI expression not supported\n");
				return 1;
			}
			expr += u.s16;
			break;

		case DW_OP_dup:
			value = POP;
			PUSH(value);
			PUSH(value);
			break;
		case DW_OP_drop:
			POP;
			break;
		case DW_OP_swap: {
			unsigned long tos = POP;
			unsigned long nos = POP;
			PUSH(tos);
			PUSH(nos);
			break;
		};

		case DW_OP_over:
			value = 1;
			goto pick;
		case DW_OP_pick:
			NEED(1);
			value = *expr++;
		pick:
			if (value >= sp)
				goto underflow;
			value = stack[sp - value];
			PUSH(value);
			break;

#define CONSTANT(type) \
			NEED(sizeof(u.type)); \
			memcpy(&u.type, expr, sizeof(u.type)); \
			expr += sizeof(u.type); \
			value = u.type; \
			PUSH(value); \
			break

		case DW_OP_addr:
			if (sizeof(unsigned long) == 8) { /* XXX 32/64!! */
				CONSTANT(u64);
			} else {
				CONSTANT(u32);
			}
			break;

		case DW_OP_const1u: CONSTANT(u8);
		case DW_OP_const1s: CONSTANT(s8);
		case DW_OP_const2u: CONSTANT(u16);
		case DW_OP_const2s: CONSTANT(s16);
		case DW_OP_const4u: CONSTANT(u32);
		case DW_OP_const4s: CONSTANT(s32);
		case DW_OP_const8u: CONSTANT(u64);
		case DW_OP_const8s: CONSTANT(s64);

#undef	CONSTANT

		case DW_OP_constu:
			value = get_uleb128(&expr, end);
			PUSH(value);
			break;
		case DW_OP_consts:
			value = get_sleb128(&expr, end);
			PUSH(value);
			break;

		case DW_OP_lit0 ... DW_OP_lit31:
			PUSH(op - DW_OP_lit0);
			break;

		case DW_OP_plus_uconst:
			value = get_uleb128(&expr, end);
			PUSH(value + POP);
			break;

#define BINOP(name, operator)				\
			case DW_OP_##name: {		\
				long b = POP;		\
				long a = POP;		\
				PUSH(a operator b);	\
			} break

			BINOP(eq, ==);
			BINOP(ne, !=);
			BINOP(ge, >=);
			BINOP(gt, >);
			BINOP(le, <=);
			BINOP(lt, <);

			BINOP(and, &);
			BINOP(or, |);
			BINOP(xor, ^);
			BINOP(plus, +);
			BINOP(minus, -);
			BINOP(mul, *);
			BINOP(div, /);
			BINOP(mod, %);
			BINOP(shl, <<);
			BINOP(shra, >>);
#undef	BINOP

		case DW_OP_shr: {
			unsigned long b = POP;
			unsigned long a = POP;
			PUSH (a >> b);
		}

		case DW_OP_not:
			PUSH(~ POP);
			break;
		case DW_OP_neg:
			PUSH(- POP);
			break;
		case DW_OP_abs:
			value = POP;
			value = abs(value);
			PUSH(value);
			break;

		case DW_OP_bregx:
			value = get_uleb128(&expr, end);
			goto breg;
		case DW_OP_breg0 ... DW_OP_breg31:
			value = op - DW_OP_breg0;
		breg:
			if (unlikely(value >= ARRAY_SIZE(reg_info))) {
				_stp_warn("invalid register number %lu in CFI expression\n", value);
				return 1;
			} else {
				sleb128_t offset = get_sleb128(&expr, end);
				value = FRAME_REG(value, unsigned long);
				PUSH(value + offset);
			}
			break;

		case DW_OP_deref:
			value = sizeof(long); /* XXX 32/64!! */
			goto deref;
		case DW_OP_deref_size:
			NEED(1);
			value = *expr++;
			if (unlikely(value > sizeof(stack[0]))) {
			bad_deref_size:
				_stp_warn("invalid DW_OP_deref_size %lu in CFI expression\n", value);
				return 1;
			}
		deref: {
				unsigned long addr = POP;
				switch (value) {
#define CASE(n)     		case sizeof(u##n):			\
					if (unlikely(_stp_read_address(value, (u##n *)addr, KERNEL_DS))) \
						goto copy_failed;	\
					break
					CASES;
#undef CASE
				default:
					goto bad_deref_size;
				}
			}
			break;

		case DW_OP_rot:
		default:
			_stp_warn("unimplemented CFI expression operation: 0x%x\n", op);
			return 1;
		}
	}

	*result = POP;
	return 0;

copy_failed:
	_stp_warn("_stp_read_address failed to access memory\n");
	return 1;
truncated:
	_stp_warn("invalid (truncated) DWARF expression in CFI\n");
	return 1;
overflow:
	_stp_warn("DWARF expression stack overflow in CFI\n");
	return 1;
underflow:
	_stp_warn("DWARF expression stack underflow in CFI\n");
	return 1;

#undef	NEED
#undef	PUSH
#undef	POP
}

/* Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error.  A positive return means unwinding is finished;
 * don't try to fallback to dumping addresses on the stack. */
static int unwind_frame(struct unwind_context *context,
			struct task_struct *tsk,
			struct _stp_module *m, struct _stp_section *s,
			void *table, uint32_t table_len, int is_ehframe)
{
	const u32 *fde = NULL, *cie = NULL;
	const u8 *ptr = NULL, *end = NULL;
	struct unwind_frame_info *frame = &context->info;
	unsigned long pc = UNW_PC(frame) - frame->call_frame;
	unsigned long tableSize, startLoc = 0, endLoc = 0, cfa;
	unsigned i;
	signed ptrType = -1;
	uleb128_t retAddrReg = 0;
	struct unwind_state *state = &context->state;
	unsigned long addr;

	if (unlikely(table_len == 0)) {
		// Don't _stp_warn about this, debug_frame and/or eh_frame
		// might actually not be there.
		dbug_unwind(1, "Module %s: no unwind frame data", m->name);
		goto err;
	}
	if (unlikely(table_len & (sizeof(*fde) - 1))) {
		_stp_warn("Module %s: frame_len=%d", m->name, table_len);
		goto err;
	}

	fde = _stp_search_unwind_hdr(pc, tsk, m, s, is_ehframe);
	dbug_unwind(1, "%s: fde=%lx\n", m->name, (unsigned long) fde);

	/* found the fde, now set startLoc and endLoc */
	if (fde != NULL) {
		cie = cie_for_fde(fde, table, table_len, is_ehframe);
		dbug_unwind(1, "%s: cie=%lx\n", m->name, (unsigned long) cie);
		if (likely(cie != NULL && cie != &bad_cie && cie != &not_fde)) {
			ptr = (const u8 *)(fde + 2);
			ptrType = fde_pointer_type(cie, table, table_len);
			startLoc = read_pointer(&ptr, (const u8 *)(fde + 1) + *fde, ptrType);
			startLoc = adjustStartLoc(startLoc, tsk, m, s, ptrType, is_ehframe);

			dbug_unwind(2, "startLoc=%lx, ptrType=%s\n", startLoc, _stp_eh_enc_name(ptrType));
			if (!(ptrType & DW_EH_PE_indirect))
				ptrType &= DW_EH_PE_FORM | DW_EH_PE_signed;
			endLoc = startLoc + read_pointer(&ptr, (const u8 *)(fde + 1) + *fde, ptrType);
			if (pc > endLoc) {
                                dbug_unwind(1, "pc (%lx) > endLoc(%lx)\n", pc, endLoc);
				goto done;
			}
		} else {
			dbug_unwind(1, "fde found in header, but cie is bad!\n");
			fde = NULL;
		}
	}

	/* did not a good fde find with binary search, so do slow linear search */
	if (fde == NULL) {
	    for (fde = table, tableSize = table_len; cie = NULL, tableSize > sizeof(*fde)
		 && tableSize - sizeof(*fde) >= *fde; tableSize -= sizeof(*fde) + *fde, fde += 1 + *fde / sizeof(*fde)) {
			dbug_unwind(3, "fde=%lx tableSize=%d\n", (long)*fde, (int)tableSize);
			cie = cie_for_fde(fde, table, table_len, is_ehframe);
			if (cie == &bad_cie) {
				cie = NULL;
				break;
			}
			if (cie == NULL || cie == &not_fde || (ptrType = fde_pointer_type(cie, table, table_len)) < 0)
				continue;

			ptr = (const u8 *)(fde + 2);
			startLoc = read_pointer(&ptr, (const u8 *)(fde + 1) + *fde, ptrType);
			startLoc = adjustStartLoc(startLoc, tsk, m, s, ptrType, is_ehframe);
			dbug_unwind(2, "startLoc=%lx, ptrType=%s\n", startLoc, _stp_eh_enc_name(ptrType));
			if (!startLoc)
				continue;
			if (!(ptrType & DW_EH_PE_indirect))
				ptrType &= DW_EH_PE_FORM | DW_EH_PE_signed;
			endLoc = startLoc + read_pointer(&ptr, (const u8 *)(fde + 1) + *fde, ptrType);
			dbug_unwind(3, "endLoc=%lx\n", endLoc);
			if (pc >= startLoc && pc < endLoc)
				break;
		}
	}

	dbug_unwind(1, "cie=%lx fde=%lx startLoc=%lx endLoc=%lx, pc=%lx\n",
                    (unsigned long) cie, (unsigned long)fde, (unsigned long) startLoc, (unsigned long) endLoc, pc);
	if (cie == NULL || fde == NULL)
		goto err;

	/* found the CIE and FDE */

	memset(state, 0, sizeof(*state));
	state->cieEnd = ptr;	/* keep here temporarily */
	ptr = (const u8 *)(cie + 2);
	end = (const u8 *)(cie + 1) + *cie;

	/* end should fall within unwind table. */
	if (((void *)end) < table
	    || ((void *)end) > ((void *)(table + table_len)))
	  goto err;

	frame->call_frame = 1;
	state->version = *ptr;
	if (state->version != 1 && state->version != 3 && state->version != 4) {
		_stp_warn("CIE version number is %d.  1, 3 or 4 is supported.\n", state->version);
		goto err;	/* unsupported version */
	}
	if (*++ptr) {
		/* check if augmentation size is first (and thus present) */
		if (*ptr == 'z') {
			while (++ptr < end && *ptr) {
				switch (*ptr) {
					/* check for ignorable (or already handled)
					 * nul-terminated augmentation string */
				case 'L':
				case 'P':
				case 'R':
					continue;
				case 'S':
					dbug_unwind(1, "This is a signal frame\n");
					frame->call_frame = 0;
					continue;
				default:
					break;
				}
				break;
			}
		}
		if (ptr >= end || *ptr) {
			_stp_warn("Problem parsing the augmentation string.\n");
			goto err;
		}
	}
	++ptr;

	/* get code aligment factor */
	state->codeAlign = get_uleb128(&ptr, end);
	/* get data aligment factor */
	state->dataAlign = get_sleb128(&ptr, end);
	if (state->codeAlign == 0 || state->dataAlign == 0 || ptr >= end)
		goto err;;

	retAddrReg = state->version <= 1 ? *ptr++ : get_uleb128(&ptr, end);

	/* skip augmentation */
	if (((const char *)(cie + 2))[1] == 'z') {
		uleb128_t augSize = get_uleb128(&ptr, end);
		ptr += augSize;
	}
	if (ptr > end || retAddrReg >= ARRAY_SIZE(reg_info)
	    || REG_INVALID(retAddrReg)
	    || reg_info[retAddrReg].width != sizeof(unsigned long))
		goto err;

	state->cieStart = ptr;
	ptr = state->cieEnd;
	state->cieEnd = end;
	end = (const u8 *)(fde + 1) + *fde;

	/* end should fall within unwind table. */
	if (((void*)end) < table
	    || ((void *)end) > ((void *)(table + table_len)))
	  goto err;

	/* skip augmentation */
	if (((const char *)(cie + 2))[1] == 'z') {
		uleb128_t augSize = get_uleb128(&ptr, end);
		if ((ptr += augSize) > end)
			goto err;
	}

	state->org = startLoc;
	memcpy(&state->cfa, &badCFA, sizeof(state->cfa));
	/* process instructions */
	if (!processCFI(ptr, end, pc, ptrType, state)
	    || state->loc > endLoc || state->regs[retAddrReg].where == Nowhere || state->cfa.reg >= ARRAY_SIZE(reg_info)
	    || reg_info[state->cfa.reg].width != sizeof(unsigned long)
	    || state->cfa.offs % sizeof(unsigned long))
		goto err;

	/* update frame */
#ifndef CONFIG_AS_CFI_SIGNAL_FRAME
	if (frame->call_frame && !UNW_DEFAULT_RA(state->regs[retAddrReg], state->dataAlign))
		frame->call_frame = 0;
#endif
	if (state->cfa_is_expr) {
		if (compute_expr(state->cfa_expr, frame, &cfa))
			goto err;
	}
	else
		cfa = FRAME_REG(state->cfa.reg, unsigned long) + state->cfa.offs;
	startLoc = min((unsigned long)UNW_SP(frame), cfa);
	endLoc = max((unsigned long)UNW_SP(frame), cfa);
	dbug_unwind(1, "cfa=%lx startLoc=%lx, endLoc=%lx\n", cfa, startLoc, endLoc);
	if (STACK_LIMIT(startLoc) != STACK_LIMIT(endLoc)) {
		startLoc = min(STACK_LIMIT(cfa), cfa);
		endLoc = max(STACK_LIMIT(cfa), cfa);
		dbug_unwind(1, "cfa startLoc=%lx, endLoc=%lx\n",
                            (unsigned long)startLoc, (unsigned long)endLoc);
	}
	dbug_unwind(1, "cie=%lx fde=%lx\n", (unsigned long) cie, (unsigned long) fde);
	for (i = 0; i < ARRAY_SIZE(state->regs); ++i) {
		if (REG_INVALID(i)) {
			if (state->regs[i].where == Nowhere)
				continue;
			_stp_warn("REG_INVALID %d\n", i);
			goto err;
		}
		dbug_unwind(2, "register %d. where=%d\n", i, state->regs[i].where);
		switch (state->regs[i].where) {
		default:
			break;
		case Register:
			if (state->regs[i].value >= ARRAY_SIZE(reg_info)
			    || REG_INVALID(state->regs[i].value)
			    || reg_info[i].width > reg_info[state->regs[i].value].width) {
				_stp_warn("case Register bad\n");
				goto err;
			}
			switch (reg_info[state->regs[i].value].width) {
#define CASE(n) \
			case sizeof(u##n): \
				state->regs[i].value = FRAME_REG(state->regs[i].value, \
				                                const u##n); \
				break
				CASES;
#undef CASE
			default:
				_stp_warn("bad Register size\n");
				goto err;
			}
			break;
		}
	}
	for (i = 0; i < ARRAY_SIZE(state->regs); ++i) {
		dbug_unwind(2, "register %d. invalid=%d\n", i, REG_INVALID(i));
		if (REG_INVALID(i))
			continue;
		dbug_unwind(2, "register %d. where=%d\n", i, state->regs[i].where);
		switch (state->regs[i].where) {
		case Nowhere:
			if (reg_info[i].width != sizeof(UNW_SP(frame))
			    || &FRAME_REG(i, __typeof__(UNW_SP(frame)))
			    != &UNW_SP(frame))
				continue;
			UNW_SP(frame) = cfa;
			break;
		case Register:
			switch (reg_info[i].width) {
#define CASE(n) case sizeof(u##n): \
				FRAME_REG(i, u##n) = state->regs[i].value; \
				break
				CASES;
#undef CASE
			default:
				_stp_warn("bad Register size\n");
				goto err;
			}
			break;
		case Expr:
			if (compute_expr(state->regs[i].expr, frame, &addr))
				goto err;
			goto memory;
		case ValExpr:
			if (compute_expr(state->regs[i].expr, frame, &addr))
				goto err;
			goto value;
		case Value:
			addr = cfa + state->regs[i].value * state->dataAlign;
		value:
			if (reg_info[i].width != sizeof(unsigned long)) {
				_stp_warn("bad Register width\n");
				goto err;
			}
			FRAME_REG(i, unsigned long) = addr;
			break;
		case Memory:
			addr = cfa + state->regs[i].value * state->dataAlign;
		memory:
			dbug_unwind(2, "addr=%lx width=%d\n", addr, reg_info[i].width);
			switch (reg_info[i].width) {
#define CASE(n)     case sizeof(u##n):					\
				if (unlikely(_stp_read_address(FRAME_REG(i, u##n), (u##n *)addr, KERNEL_DS))) \
					goto copy_failed;		\
				dbug_unwind(1, "set register %d to %lx\n", i, (long)FRAME_REG(i,u##n));	\
				break
				CASES;
#undef CASE
			default:
				_stp_warn("bad Register width\n");
				goto err;
			}
			break;
		}
	}
	dbug_unwind(1, "returning 0 (%lx)\n", UNW_PC(frame));
	return 0;

copy_failed:
	_stp_warn("_stp_read_address failed to access memory\n");
err:
	return -EIO;

done:
	/* PC was in a range convered by a module but no unwind info */
	/* found for the specific PC. This seems to happen only for kretprobe */
	/* trampolines and at the end of interrupt backtraces. */
	return 1;
#undef CASES
#undef FRAME_REG
}

static int unwind(struct unwind_context *context,
		  struct task_struct *tsk)
{
	struct _stp_module *m;
	struct _stp_section *s = NULL;
	struct unwind_frame_info *frame = &context->info;
	unsigned long pc = UNW_PC(frame) - frame->call_frame;
	int res;
        const char *module_name = 0;

	dbug_unwind(1, "pc=%lx, %lx", pc, UNW_PC(frame));

	if (UNW_PC(frame) == 0)
		return -EINVAL;

	if (tsk)
	  {
	    m = _stp_umod_lookup (pc, tsk, & module_name, NULL, NULL);
	    if (m)
	      s = &m->sections[0];
	  }
	else
          {
            preempt_disable(); /* probably redundant */
            m = _stp_kmod_sec_lookup (pc, &s);
            if (!m) {
#ifdef STAPCONF_MODULE_TEXT_ADDRESS
                struct module *ko = __module_text_address (pc);
                if (ko) { module_name = ko->name; }
                else { 
                  /* Possible heuristic: we could assume we're talking
                     about the kernel.  If __kernel_text_address()
                     were SYMBOL_EXPORT'd, we could call that and be
                     more sure. */
                } 
#endif
            }
            preempt_enable_no_resched();
          }

	if (unlikely(m == NULL)) {
                if (module_name)
                        _stp_warn ("Missing unwind data for module, rerun with 'stap -d %s'\n",
                                   module_name); 
		// Don't _stp_warn about this, will use fallback unwinder.
		dbug_unwind(1, "No module found for pc=%lx", pc);
		return -EINVAL;
	}

	dbug_unwind(1, "trying debug_frame\n");
	res = unwind_frame (context, tsk, m, s, m->debug_frame,
			    m->debug_frame_len, 0);
	if (res != 0) {
	  dbug_unwind(1, "debug_frame failed: %d, trying eh_frame\n", res);
	  res = unwind_frame (context, tsk, m, s, m->eh_frame,
			      m->eh_frame_len, 1);
	}

        /* This situation occurs where some unwind data was found, but
           it was lacking somehow.  */
        if (res != 0) {
                dbug_unwind (2, "unwinding failed: %d\n", res);
        }

	return res;
}

#else

struct unwind_context { };

#endif /* STP_USE_DWARF_UNWINDER */
