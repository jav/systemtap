#ifndef _SYM_C_ /* -*- linux-c -*- */
#define _SYM_C_

#include "scbuf.c"

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

static void __stp_symbol_print (unsigned long address, void (*prtfunc)(const char *fmt, ...))
{
        char *modname;
        const char *name;
        unsigned long offset, size;
        char namebuf[KSYM_NAME_LEN+1];

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	(*prtfunc)("0x%lx : ", address);
	if (modname)
		(*prtfunc)("%s+%#lx/%#lx [%s]", name, offset, size, modname);
	else
		(*prtfunc)("%s+%#lx/%#lx", name, offset, size);
}

/** Print addresses symbolically into a string
 * @param address The address to lookup.
 * @note Symbolic lookups should not be done within
 * a probe because it is too time-consuming. Use at module exit time.
 * @note Uses scbuf.
 */

char * _stp_symbol_sprint (unsigned long address)
{ 
	char *ptr = _stp_scbuf_cur();
	__stp_symbol_print (address, _stp_sprint);
	return ptr;
}


/** Print addresses symbolically to the trace buffer.
 * @param address The address to lookup.
 * @note Symbolic lookups should not be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

void _stp_symbol_print (unsigned long address)
{
	__stp_symbol_print (address, _stp_print);
}

/** @} */
#endif /* _SYM_C_ */
