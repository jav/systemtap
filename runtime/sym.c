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
		if (!strcmp(module, last->name) && !strcmp(section, last_sec->symbol)) {
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
            if (!strcmp(section, last_sec->symbol)) {
              offset += last_sec->addr;
              dbug_sym(1, "address=%lx\n", offset);
              return offset;
            }
          }
	}

	last = NULL;
	return 0;
}


/* Return the module that likely contains the given address.  */
/* XXX: This query only makes sense with respect to a particular
   address space.  A more general interface would have to identify
   the address space, and also pass back the section. */ 
static struct _stp_module *_stp_find_module_by_addr(unsigned long addr)
{
  unsigned i;
  struct _stp_module *closest_module = NULL;
  unsigned long closest_module_offset = ~0; /* minimum[addr - module->.text] */
  
  for (i=0; i<_stp_num_modules; i++)
    {
      unsigned long module_text_addr, this_module_offset;

      if (_stp_modules[i]->num_sections < 1) continue;
      module_text_addr = _stp_modules[i]->sections[0].addr; /* XXX: assume section[0]=>text */
      if (addr < module_text_addr) continue;
      this_module_offset = module_text_addr - addr;
      
      if (this_module_offset < closest_module_offset)
        {
          closest_module = _stp_modules[i];
          closest_module_offset = this_module_offset;
        }
    }

  return closest_module;
}



static const char *_stp_kallsyms_lookup(unsigned long addr, unsigned long *symbolsize,
                                        unsigned long *offset, char **modname, char *namebuf)
{
	struct _stp_module *m;
	struct _stp_symbol *s;
	unsigned long flags;
	unsigned end, begin = 0;

	m = _stp_find_module_by_addr(addr);
	if (unlikely(m == NULL)) {
		return NULL;
	}

        /* NB: relativize the address to the (XXX) presumed text section. */
        addr -= m->sections[0].addr;
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
			return namebuf;
		} else {
			return s->symbol;
		}
	}
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
int _stp_func_print(unsigned long address, int verbose, int exact)
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
		return 1;
	}
	return 0;
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
