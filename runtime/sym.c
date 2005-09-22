#ifndef _SYM_C_ /* -*- linux-c -*- */
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
#ifdef HAS_LOOKUP
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
#else
	_stp_sprintf (str, "0x%lx", address);
#endif
	return str;
}


/** Print addresses symbolically to the print buffer.
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

#define _stp_symbol_print(address) _stp_symbol_sprint(_stp_stdout,address)

/** @} */
#endif /* _SYM_C_ */
