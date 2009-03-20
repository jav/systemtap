/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005-2009 Red Hat Inc.
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

/* Callback that needs to be registered (in tapsets.cxx for
   emit_module_init) for every user task path or pid for which we
   might need symbols or unwind info. */
static int _stp_tf_vm_cb(struct stap_task_finder_target *tgt,
			 struct task_struct *tsk,
			 int map_p, char *vm_path,
			 unsigned long vm_start, unsigned long vm_end,
			 unsigned long vm_pgoff)
{
  int i;
#ifdef DEBUG_TASK_FINDER_VMA
  _stp_dbug(__FUNCTION__, __LINE__, "vm_cb: tsk %d:%d path %s, start 0x%08lx, end 0x%08lx, offset 0x%lx\n", tsk->pid, map_p, vm_path, vm_start, vm_end, vm_pgoff);
#endif
  if (map_p)
    {
      struct _stp_module *module = NULL;
      if (vm_path != NULL)
	for (i = 0; i < _stp_num_modules; i++)
	  if (strcmp(vm_path, _stp_modules[i]->path) == 0)
	    {
#ifdef DEBUG_TASK_FINDER_VMA
	      _stp_dbug(__FUNCTION__, __LINE__, "vm_cb: matched path %s to module\n", vm_path);
#endif
	      module = _stp_modules[i];
	      break;
	    }
      stap_add_vma_map_info(tsk, vm_start, vm_end, vm_pgoff, module);
    }
  else
    stap_remove_vma_map_info(tsk, vm_start, vm_end, vm_pgoff);

  return 0;
}

/* XXX: this needs to be address-space-specific. */
static unsigned long _stp_module_relocate(const char *module, const char *section, unsigned long offset)
{
	static struct _stp_module *last = NULL;
	static struct _stp_section *last_sec;
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
					       struct task_struct *task,
					       struct _stp_section **sec)
{
  void *user = NULL;
  struct _stp_module *m = NULL;
  unsigned midx = 0;
  unsigned long closest_section_offset = ~0;

  // Try vma matching first if task given.
  if (task)
    {
      unsigned long vm_start = 0;
      if (stap_find_vma_map_info(task, addr,
				 &vm_start, NULL,
				 NULL, &user) == 0)
	if (user != NULL)
	  {
	    m = (struct _stp_module *)user;
	    *sec = &m->sections[0]; // XXX check actual section and relocate
	    dbug_sym(1, "found section %s in module %s at 0x%lx\n",
		     m->sections[0].name, m->name, vm_start);
	    if (strcmp(".dynamic", m->sections[0].name) == 0)
	      m->sections[0].addr = vm_start; // cheat...
	    return m;
	  }
    }

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
                                        char *namebuf,
					struct task_struct *task)
{
	struct _stp_module *m = NULL;
	struct _stp_section *sec = NULL;
	struct _stp_symbol *s = NULL;
	unsigned end, begin = 0;

	m = _stp_mod_sec_lookup(addr, task, &sec);
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
		if (m->build_id_len > 0 && m->notes_sect != 0) {
		    dbug_sym(1, "build-id validation [%s]\n", m->name);

		    /* notes end address */
		    if (!strcmp(m->name, "kernel")) {
		  	  notes_addr = m->build_id_offset;
			  base_addr = _stp_module_relocate("kernel",
							   "_stext", 0);
                    } else {
			  notes_addr = m->notes_sect + m->build_id_offset;
			  base_addr = m->notes_sect;
		    }

		    /* build-id note payload start address */
                    /* XXX: But see https://bugzilla.redhat.com/show_bug.cgi?id=465872;
                       dwfl_module_build_id was not intended to return the end address. */
		    notes_addr -= m->build_id_len;

		    if (notes_addr > base_addr) {
		      for (j = 0; j < m->build_id_len; j++)	 
                        {
                          unsigned char theory, practice;
                          theory = m->build_id_bits [j];
                          practice = ((unsigned char*) notes_addr) [j];
                          /* XXX: consider using kread() instead of above. */
                          if (theory != practice)
                            {
                              #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
                              _stp_error ("%s: inconsistent %s build-id byte #%d "
                                          "(0x%x [actual] vs. 0x%x [debuginfo])\n",
                                          THIS_MODULE->name, m->name, j,
                                          practice, theory);
                              return 1;
                              #else
                              /* This branch is a surrogate for
                                 kernels affected by Fedora bug
                                 #465873. */
                              printk(KERN_WARNING
                                     "%s: inconsistent %s build-id byte #%d "
                                     "(0x%x [actual] vs. 0x%x [debuginfo])\n",
                                     THIS_MODULE->name, m->name, j,
                                     practice, theory);
                              break; /* Note just the first mismatch. */
                              #endif
                            }
			} 
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

static void _stp_symbol_print(unsigned long address)
{
	const char *modname;
	const char *name;
	unsigned long offset, size;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL, NULL);

	_stp_printf("%p", (int64_t) address);

	if (name) {
		if (modname && *modname)
			_stp_printf(" : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_printf(" : %s+%#lx/%#lx", name, offset, size);
	}
}

/* Like _stp_symbol_print, except only print if the address is a valid function address */
static int _stp_func_print(unsigned long address, int verbose, int exact)
{
	const char *modname;
	const char *name;
	unsigned long offset, size;
	char *exstr;

	if (exact)
		exstr = "";
	else
		exstr = " (inexact)";

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL, NULL);

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

static void _stp_symbol_snprint(char *str, size_t len, unsigned long address,
			 struct task_struct *task)
{
	const char *modname;
	const char *name;
	unsigned long offset, size;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL,
				    task);
	if (name)
		strlcpy(str, name, len);
	else
		_stp_snprintf(str, len, "%p", (int64_t) address);
}

/** @} */
#endif /* _STP_SYM_C_ */
