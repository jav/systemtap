/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005, 2006, 2007 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _SYM_C_
#define _SYM_C_

#include "string.c"

/** @file sym.c
 * @addtogroup sym Symbolic Functions
 * Symbolic Lookup Functions
 * @{
 */

unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset) {
	static struct _stp_module *last = NULL;
	static struct _stp_symbol *last_sec;
	unsigned long flags;
	int i,j;

	/* if module is -1, we invalidate last. _stp_del_module calls this when modules are deleted. */
	if ((long)module == -1) {
		last = NULL;
		return 0;
	}

	dbug("_stp_relocate_module: %s, %s, %lx\n", module, section, offset);

	STP_LOCK_MODULES;
	if (! module || _stp_num_modules == 0) {
		STP_UNLOCK_MODULES;
		return offset; 
	}

	/* Most likely our relocation is in the same section of the same module as the last. */
	if (last) {
		if (!strcmp (module, last->name) && !strcmp (section, last_sec->symbol)) {
			offset += last_sec->addr;
			STP_UNLOCK_MODULES;
			return offset;
		}
	}

	/* not cached. need to scan all modules */
        if (! strcmp (module, "kernel")) {
		STP_UNLOCK_MODULES;

		/* See also transport/symbols.c (_stp_do_symbols). */
		if (strcmp (section, "_stext"))
			return 0;
		else
			return offset + _stp_modules[0]->text;
	} else {
		/* relocatable module */
		for (i = 1; i < _stp_num_modules; i++) { /* skip over [0]=kernel */
			last = _stp_modules[i];
			if (strcmp(module, last->name))
				continue;
			for (j = 0; j < (int)last->num_sections; j++) {
				last_sec = &last->sections[j];
				if (!strcmp (section, last_sec->symbol)) {
					offset += last_sec->addr;
					STP_UNLOCK_MODULES;
					return offset;
				}
			}
		}
	}
	STP_UNLOCK_MODULES;
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

static const char * _stp_kallsyms_lookup (
	unsigned long addr,
	unsigned long *symbolsize,
	unsigned long *offset,
	char **modname,
	char *namebuf)
{
	struct _stp_module *m;
	struct _stp_symbol *s;
	unsigned long flags;
	unsigned end, begin = 0;

	if (STP_TRYLOCK_MODULES)
		return NULL;

	end = _stp_num_modules;

	if (_stp_num_modules >= 2 && addr > _stp_modules_by_addr[1]->text) {
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
	m = _stp_modules_by_addr[begin];
	begin = 0;
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
	if (addr < s->addr) {
		STP_UNLOCK_MODULES;
		return NULL;
	} else {
		if (offset) *offset = addr - s->addr;
		if (modname) *modname = m->name;
		if (symbolsize) {
			if ((begin + 1) < m->num_symbols)
				*symbolsize = m->symbols[begin+1].addr - s->addr;
			else
				*symbolsize = 0;
			// NB: This is only a heuristic.  Sometimes there are large
			// gaps between text areas of modules.
		}
		if (namebuf) {
			strlcpy (namebuf, s->symbol, KSYM_NAME_LEN+1);
			STP_UNLOCK_MODULES;
			return namebuf;
		}
		else {
			STP_UNLOCK_MODULES;
			return s->symbol;
		}
	}
	STP_UNLOCK_MODULES;
	return NULL;
}

/** Print an address symbolically.
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

void _stp_symbol_print (unsigned long address)
{ 
	char *modname;
        const char *name;
        unsigned long offset, size;

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL);

	_stp_printf ("%p", (void *)address);

	if (name) {		
		if (modname)
			_stp_printf (" : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_printf (" : %s+%#lx/%#lx", name, offset, size);
	}
}

void _stp_symbol_snprint (char *str, size_t len, unsigned long address)
{ 
    char *modname;
    const char *name;
    unsigned long offset, size;

    name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL);
    if (name)
	    strlcpy(str, name, len);
    else
	    snprintf(str, len, "%p", (void *)address);
}

/** @} */
#endif /* _SYM_C_ */
