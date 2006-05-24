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

	_stp_sprintf (str, "0x%lx", address);

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
            snprintf(str, len, "0x%lx", address);
    } else {
        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);
        if (name)
            strlcpy(str, namebuf, len);
        else
            snprintf(str, len, "0x%lx", address);
    }

    return str;
}

/** @} */
#endif /* _SYM_C_ */
