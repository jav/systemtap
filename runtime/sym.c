#ifndef _SYM_C_ /* -*- linux-c -*- */
#define _SYM_C_

#include "string.c"

/** @file sym.c
 * @addtogroup sym Symbolic Functions
 * Symbolic Lookup Functions
 * @{
 */

/** Lookup symbol.
 * This simply calls the kernel function kallsyms_lookup().
 * That function is not exported, so this workaround is required.
 * See the kernel source, kernel/kallsyms.c for more information.
 */
static const char * (*_stp_kallsyms_lookup)(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)=(void *)KALLSYMS_LOOKUP;


/** Print addresses symbolically into a string
 * @param address The address to lookup.
 * @note Symbolic lookups should not be done within
 * a probe because it is too time-consuming. Use at module exit time.
 * @note Uses scbuf.
 */

String _stp_symbol_sprint (String str, unsigned long address)
{ 
	char *modname;
        const char *name;
        unsigned long offset, size;
        char namebuf[KSYM_NAME_LEN+1];

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	_stp_sprintf (str, "0x%lx : ", address);
	if (modname)
		_stp_sprintf (str, "%s+%#lx/%#lx [%s]", name, offset, size, modname);
	else
		_stp_sprintf (str, "%s+%#lx/%#lx", name, offset, size);
	return str;
}


/** Print addresses symbolically to the trace buffer.
 * @param address The address to lookup.
 * @note Symbolic lookups should not be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

void _stp_symbol_print (unsigned long address)
{
	char *modname;
        const char *name;
        unsigned long offset, size;
        char namebuf[KSYM_NAME_LEN+1];

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	_stp_printf ("0x%lx : ", address);
	if (modname)
		_stp_printf ("%s+%#lx/%#lx [%s]", name, offset, size, modname);
	else
		_stp_printf ("%s+%#lx/%#lx", name, offset, size);
}

/** @} */
#endif /* _SYM_C_ */
