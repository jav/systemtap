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

/* Lookup the kernel address for this symbol. Returns 0 if not found. */
static unsigned long _stp_kallsyms_lookup_name(const char *name)
{
	struct stap_symbol *s = &stap_symbols[0];
	unsigned num = stap_num_symbols;

	/* Warning: Linear search. If this function ends up being used in */
	/* time-critical places, maybe we need to create a new symbol table */
	/* sorted by name. */

	while (num--) {
		if ((strcmp(name, s->symbol) == 0) && (strcmp(s->modname,"") == 0))
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
	unsigned begin = 0;
	unsigned end = stap_num_symbols;
	/*const*/ struct stap_symbol* s;

	/* binary search on index [begin,end) */
	do {
		unsigned mid = (begin + end) / 2;
		if (addr < stap_symbols[mid].addr)
			end = mid;
		else
			begin = mid;
	} while (begin + 1 < end);
	/* result index in $begin, guaranteed between [0,stap_num_symbols) */

	s = & stap_symbols [begin];
	if (addr < s->addr)
		return NULL;
	else {
		if (offset) *offset = addr - s->addr;
		if (modname) *modname = (char *) s->modname;
		if (symbolsize) {
			if ((begin + 1) < stap_num_symbols)
				*symbolsize = stap_symbols[begin+1].addr - s->addr;
			else
				*symbolsize = 0;
			// NB: This is only a heuristic.  Sometimes there are large
			// gaps between text areas of modules.
		}
		if (namebuf) {
			strlcpy (namebuf, s->symbol, KSYM_NAME_LEN+1);
			return namebuf;
		}
		else
			return s->symbol;
	}
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
