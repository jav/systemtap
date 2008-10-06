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

/* XXX: this needs to be address-space-specific. */
unsigned long _stp_module_relocate(const char *module, const char *section, unsigned long offset)
{
	static struct _stp_module *last = NULL;
	static struct _stp_section *last_sec;
	unsigned long flags;
	unsigned i, j;

	/* if module is -1, we invalidate last. _stp_del_module calls this when modules are deleted. */
	if ((long)module == -1) {
		last = NULL;
		return 0;
	}

	dbug_sym(1, "%s, %s, %lx\n", module, section, offset);

	if (!module || !strcmp(section, "")	/* absolute, unrelocated address */
	    ||_stp_num_modules == 0) {
		return offset;
	}

	/* Most likely our relocation is in the same section of the same module as the last. */
	if (last) {
		if (!strcmp(module, last->name) && !strcmp(section, last_sec->name)) {
			offset += last_sec->addr;
			dbug_sym(1, "cached address=%lx\n", offset);
			return offset;
		}
	}

        for (i = 0; i < _stp_num_modules; i++) {
          last = _stp_modules[i];
          if (strcmp(module, last->name))
            continue;
          for (j = 0; j < last->num_sections; j++) {
            last_sec = &last->sections[j];
            if (!strcmp(section, last_sec->name)) {
            
              if (last_sec->addr == 0) /* module/section not in memory */
                continue;

              offset += last_sec->addr;
              dbug_sym(1, "address=%lx\n", offset);
              return offset;
            }
          }
	}

	last = NULL;
	return 0;
}


/* Return module owner and fills in closest section of the address
   if found, return NULL otherwise.
   XXX: needs to be address-space-specific. */
static struct _stp_module *_stp_mod_sec_lookup(unsigned long addr,
					       struct _stp_section **sec)
{
  struct _stp_module *m = NULL;
  unsigned midx = 0;
  unsigned long closest_section_offset = ~0;
  for (midx = 0; midx < _stp_num_modules; midx++)
    {
      unsigned secidx;
      for (secidx = 0; secidx < _stp_modules[midx]->num_sections; secidx++)
	{
	  unsigned long this_section_addr;
	  unsigned long this_section_offset;
	  this_section_addr = _stp_modules[midx]->sections[secidx].addr;
	  if (addr < this_section_addr) continue;
	  this_section_offset = addr - this_section_addr;
	  if (this_section_offset < closest_section_offset)
	    {
	      closest_section_offset = this_section_offset;
	      m = _stp_modules[midx];
	      *sec = & m->sections[secidx];
	    }
	}
      }
  return m;
}


/* XXX: needs to be address-space-specific. */
static const char *_stp_kallsyms_lookup(unsigned long addr, unsigned long *symbolsize,
                                        unsigned long *offset, 
                                        const char **modname, 
                                        /* char ** secname? */
                                        char *namebuf)
{
	struct _stp_module *m = NULL;
	struct _stp_section *sec = NULL;
	struct _stp_symbol *s = NULL;
	unsigned long flags;
	unsigned end, begin = 0;

	m = _stp_mod_sec_lookup(addr, &sec);
        if (unlikely (m == NULL || sec == NULL))
          return NULL;
        
        /* NB: relativize the address to the section. */
        addr -= sec->addr;
	end = sec->num_symbols;

	/* binary search for symbols within the module */
	do {
		unsigned mid = (begin + end) / 2;
		if (addr < sec->symbols[mid].addr)
			end = mid;
		else
			begin = mid;
	} while (begin + 1 < end);
	/* result index in $begin */

	s = & sec->symbols[begin];
	if (likely(addr >= s->addr)) {
		if (offset)
			*offset = addr - s->addr;
		if (modname)
			*modname = m->name;
                /* We could also pass sec->name here. */
		if (symbolsize) {
			if ((begin + 1) < sec->num_symbols)
				*symbolsize = sec->symbols[begin + 1].addr - s->addr;
			else
				*symbolsize = 0;
			// NB: This is only a heuristic.  Sometimes there are large
			// gaps between text areas of modules.
		}
		if (namebuf) {
			strlcpy(namebuf, s->symbol, KSYM_NAME_LEN + 1);
			return namebuf;
		} else {
			return s->symbol;
		}
	}
	return NULL;
}

/* Validate module/kernel based on build-id if there 
*  The completed case is the following combination:
*	   Debuginfo 		 Module			         Kernel	
* 			   X				X
* 	has build-id/not	unloaded		      has build-id/not	
*				loaded && (has build-id/not)  
*
*  NB: build-id exists only if ld>=2.18 and kernel>= 2.6.23
*/
static int _stp_module_check(void)
{
	struct _stp_module *m = NULL;
	unsigned long notes_addr, base_addr;
	unsigned i,j;

	for (i = 0; i < _stp_num_modules; i++)
	{
		m = _stp_modules[i];			

		/* unloaded module */
		if (m->notes_sect == 0) {
              	     _stp_warn("skip checking %s\n", m->name);
                    continue;
                }
		if (m->build_id_len > 0) { /* build-id in debuginfo file */
		    dbug_sym(1, "validate %s based on build-id\n", m->name);

		    /* loaded module/kernel, but without build-id */
		    if (m->notes_sect == 1) {
		   	_stp_error("missing build-id in %s\n", m->name);
			return 1;
		    } 
		    /* notes end address */
		    if (!strcmp(m->name, "kernel")) {
		  	  notes_addr = m->build_id_offset;
			  base_addr = _stp_module_relocate("kernel",
							   "_stext", 0);
                    } else {
			  notes_addr = m->notes_sect + m->build_id_offset;
			  base_addr = m->notes_sect;
		    }
		    /* notes start address */
		    notes_addr -= m->build_id_len;
		    if (notes_addr > base_addr) {
		      for (j = 0; j < m->build_id_len; j++)	 
		        if (*((unsigned char *) notes_addr+j) != 
							*(m->build_id_bits+j)) 
			{
			  _stp_error("inconsistent bit (0x%x [%s] vs 0x%x [debuginfo]) of build-id\n", *((unsigned char *) notes_addr+j), m->name, *(m->build_id_bits+j));
			  return 1;
			} 
		    } else { /* bug, shouldn't come here */
			     _stp_error("unknown failure in checking %s\n", 
								m->name);
			     return 1;
		    	   } /* end comparing */
		} else { 
			  /* build-id in module/kernel, absent in debuginfo */
			  if (m->notes_sect > 1) {
			    _stp_error("unexpected build-id in %s\n", m->name);
			    return 1;
			  }
		 } /* end checking */
	} /* end loop */
	return 0;
}

/** Print an address symbolically.
 * @param address The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

void _stp_symbol_print(unsigned long address)
{
	const char *modname;
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
int _stp_func_print(unsigned long address, int verbose, int exact)
{
	const char *modname;
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
		return 1;
	}
	return 0;
}

void _stp_symbol_snprint(char *str, size_t len, unsigned long address)
{
	const char *modname;
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
