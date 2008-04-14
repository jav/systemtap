/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005-2008 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_SYM_C_
#define _STP_SYM_C_

#include "string.c"

/** @file sym.c
 * @addtogroup sym Symbolic Functions
 * Symbolic Lookup Functions
 * @{
 */

unsigned long _stp_module_relocate(const char *module, const char *section, unsigned long offset)
{
	static struct _stp_module *last = NULL;
	static struct _stp_symbol *last_sec;
	unsigned long flags;
	int i, j;

	/* if module is -1, we invalidate last. _stp_del_module calls this when modules are deleted. */
	if ((long)module == -1) {
		last = NULL;
		return 0;
	}

	dbug_sym(1, "%s, %s, %lx\n", module, section, offset);

	STP_RLOCK_MODULES;
	if (!module || !strcmp(section, "")	/* absolute, unrelocated address */
	    ||_stp_num_modules == 0) {
		STP_RUNLOCK_MODULES;
		return offset;
	}

	/* Most likely our relocation is in the same section of the same module as the last. */
	if (last) {
		if (!strcmp(module, last->name) && !strcmp(section, last_sec->symbol)) {
			offset += last_sec->addr;
			STP_RUNLOCK_MODULES;
			dbug_sym(1, "offset = %lx\n", offset);
			return offset;
		}
	}

	/* not cached. need to scan all modules */
	if (!strcmp(module, "kernel")) {
		STP_RUNLOCK_MODULES;

		/* See also transport/symbols.c (_stp_do_symbols). */
		if (strcmp(section, "_stext"))
			return 0;
		else
			return offset + _stp_modules[0]->text;
	} else {
		/* relocatable module */
		for (i = 1; i < _stp_num_modules; i++) {	/* skip over [0]=kernel */
			last = _stp_modules[i];
			if (strcmp(module, last->name))
				continue;
			for (j = 0; j < (int)last->num_sections; j++) {
				last_sec = &last->sections[j];
				if (!strcmp(section, last_sec->symbol)) {
					offset += last_sec->addr;
					STP_RUNLOCK_MODULES;
					dbug_sym(1, "offset = %lx\n", offset);
					return offset;
				}
			}
		}
	}
	STP_RUNLOCK_MODULES;
	last = NULL;
	return 0;
}

/* Lookup the kernel address for this symbol. Returns 0 if not found. */
static unsigned long _stp_kallsyms_lookup_name(const char *name)
{
	struct _stp_symbol *s = _stp_modules[0]->symbols;
	unsigned num = _stp_modules[0]->num_symbols;

	while (num--) {
		if (strcmp(name, s->symbol) == 0)
			return s->addr;
		s++;
	}
	return 0;
}

static struct _stp_module *_stp_find_module_by_addr(unsigned long addr)
{
	unsigned begin = 0;
	unsigned end = _stp_num_modules;

	if (unlikely(addr < _stp_modules_by_addr[0]->text))
		return NULL;

	if (_stp_num_modules > 1 && addr > _stp_modules_by_addr[0]->data) {
		/* binary search on index [begin,end) */
		do {
			unsigned mid = (begin + end) / 2;
			if (addr < _stp_modules_by_addr[mid]->text)
				end = mid;
			else
				begin = mid;
		} while (begin + 1 < end);
		/* result index in $begin, guaranteed between [0,_stp_num_modules) */
	}
	/* check if addr is past the last module */
	if (unlikely(begin == _stp_num_modules - 1
		     && (addr > _stp_modules_by_addr[begin]->text + _stp_modules_by_addr[begin]->text_size)))
		return NULL;

	return _stp_modules_by_addr[begin];
}

static struct _stp_module *_stp_get_unwind_info(unsigned long addr)
{
	struct _stp_module *m;
	struct _stp_symbol *s;
	unsigned long flags;

	STP_RLOCK_MODULES;
	m = _stp_find_module_by_addr(addr);
	if (unlikely(m == NULL)) {
		STP_RUNLOCK_MODULES;
		return NULL;
	}
	/* Lock the module struct so it doesn't go away while being used. */
	/* Probably could never happen, but lock it to be sure for now. */
	read_lock(&m->lock);

	STP_RUNLOCK_MODULES;
	return m;
}

static const char *_stp_kallsyms_lookup(unsigned long addr,
					unsigned long *symbolsize, unsigned long *offset, char **modname, char *namebuf)
{
	struct _stp_module *m;
	struct _stp_symbol *s;
	unsigned long flags;
	unsigned end, begin = 0;

	STP_RLOCK_MODULES;
	m = _stp_find_module_by_addr(addr);
	if (unlikely(m == NULL)) {
		STP_RUNLOCK_MODULES;
		return NULL;
	}

	end = m->num_symbols;

	/* binary search for symbols within the module */
	do {
		unsigned mid = (begin + end) / 2;
		if (addr < m->symbols[mid].addr)
			end = mid;
		else
			begin = mid;
	} while (begin + 1 < end);
	/* result index in $begin */

	s = &m->symbols[begin];
	if (likely(addr >= s->addr)) {
		if (offset)
			*offset = addr - s->addr;
		if (modname)
			*modname = m->name;
		if (symbolsize) {
			if ((begin + 1) < m->num_symbols)
				*symbolsize = m->symbols[begin + 1].addr - s->addr;
			else
				*symbolsize = 0;
			// NB: This is only a heuristic.  Sometimes there are large
			// gaps between text areas of modules.
		}
		if (namebuf) {
			strlcpy(namebuf, s->symbol, KSYM_NAME_LEN + 1);
			STP_RUNLOCK_MODULES;
			return namebuf;
		} else {
			STP_RUNLOCK_MODULES;
			return s->symbol;
		}
	}
	STP_RUNLOCK_MODULES;
	return NULL;
}

/** Print an address symbolically.
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

void _stp_symbol_print(unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL);

	_stp_printf("%p", (int64_t) address);

	if (name) {
		if (modname && *modname)
			_stp_printf(" : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_printf(" : %s+%#lx/%#lx", name, offset, size);
	}
}

/* Like _stp_symbol_print, except only print if the address is a valid function address */

void _stp_func_print(unsigned long address, int verbose, int exact)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	char *exstr;

	if (exact)
		exstr = "";
	else
		exstr = " (inexact)";

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL);

	if (name) {
		if (verbose) {
			if (modname && *modname)
				_stp_printf(" %p : %s+%#lx/%#lx [%s]%s\n",
					    (int64_t) address, name, offset, size, modname, exstr);
			else
				_stp_printf(" %p : %s+%#lx/%#lx%s\n", (int64_t) address, name, offset, size, exstr);
		} else
			_stp_printf("%p ", (int64_t) address);
	}
}

void _stp_symbol_snprint(char *str, size_t len, unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL);
	if (name)
		strlcpy(str, name, len);
	else
		_stp_snprintf(str, len, "%p", (int64_t) address);
}

/** @} */
#endif /* _STP_SYM_C_ */
