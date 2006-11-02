/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005 Red Hat Inc.
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

static unsigned long _stp_module_relocate (const char *module, const char *section, unsigned long offset) {
	static struct _stp_module *last = NULL;
	static struct _stp_symbol *last_sec;
	unsigned long flags;
	int i,j;

	STP_LOCK_MODULES;
	if (! module || _stp_num_modules == 0) {
		STP_UNLOCK_MODULES;
		return offset; 
	}

	if (last) {
		if (!strcmp (module, last->name) && !strcmp (section, last_sec->symbol)) {
			STP_UNLOCK_MODULES;
			return offset + last_sec->addr;
		}
	}

	/* need to scan all modules */
	for (i = 1; i < _stp_num_modules; i++) {
		last = _stp_modules[i];
		if (strcmp(module, last->name))
			continue;
		for (j = 0; j < last->num_sections; j++) {
			last_sec = &last->sections[j];
			if (!strcmp (section, last_sec->symbol)) {
				STP_UNLOCK_MODULES;
				return offset + last_sec->addr;
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

/** Write addresses symbolically into a String
 * @param str String
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

String _stp_symbol_sprint (String str, unsigned long address)
{ 
	char *modname;
        const char *name;
        unsigned long offset, size;
        char namebuf[KSYM_NAME_LEN+1];

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	_stp_sprintf (str, "%p", (void *)address);

	if (name) {		
		if (modname)
			_stp_sprintf (str, " : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_sprintf (str, " : %s+%#lx/%#lx", name, offset, size);
	}
	return str;
}


/** Print addresses symbolically to the print buffer.
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

#define _stp_symbol_print(address) _stp_symbol_sprint(_stp_stdout,address)


/** Write addresses symbolically into a char buffer
 * @param str Destination buffer
 * @param len Length of destination buffer
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

const char *_stp_symbol_sprint_basic (char *str, size_t len, unsigned long address)
{ 
    char *modname;
    const char *name;
    unsigned long offset, size;
    char namebuf[KSYM_NAME_LEN+1];

    if (len > KSYM_NAME_LEN) {
        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, str);
        if (!name)
		snprintf(str, len, "%p", (void *)address);
    } else {
        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);
        if (name)
            strlcpy(str, namebuf, len);
        else
		snprintf(str, len, "%p", (void *)address);
    }

    return str;
}

/** @} */
#endif /* _SYM_C_ */
