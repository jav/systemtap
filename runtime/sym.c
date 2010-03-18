/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005-2010 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_SYM_C_
#define _STP_SYM_C_

#include "sym.h"
#include "string.c"
#include "task_finder_vma.c"
#include <asm/uaccess.h>

/* Callback that needs to be registered (in
   session.unwindsyms_modules) for every user task path for which we
   might need symbols or unwind info. */
static int _stp_tf_exec_cb(struct stap_task_finder_target *tgt,
			   struct task_struct *tsk,
			   int register_p,
			   int process_p)
{
#ifdef DEBUG_TASK_FINDER_VMA
  _stp_dbug(__FUNCTION__, __LINE__,
	    "tsk %d:%d , register_p: %d, process_p: %d\n",
	    tsk->pid, tsk->tgid, register_p, process_p);
#endif
  if (process_p && ! register_p)
    stap_drop_vma_maps(tsk);

  return 0;
}

static int _stp_tf_mmap_cb(struct stap_task_finder_target *tgt,
			   struct task_struct *tsk,
			   char *path,
			   unsigned long addr,
			   unsigned long length,
			   unsigned long offset,
			   unsigned long vm_flags)
{
	int i;
	struct _stp_module *module = NULL;

#ifdef DEBUG_TASK_FINDER_VMA
	_stp_dbug(__FUNCTION__, __LINE__,
		  "mmap_cb: tsk %d:%d path %s, addr 0x%08lx, length 0x%08lx, offset 0x%lx, flags 0x%lx\n",
		  tsk->pid, tsk->tgid, path, addr, length, offset, vm_flags);
#endif
	// We are only interested in the first load of the whole module that
	// is executable. But see below for the comment about PR11015.
	if (path != NULL && offset == 0 && (vm_flags & VM_EXEC)) {
		for (i = 0; i < _stp_num_modules; i++) {
			if (strcmp(path, _stp_modules[i]->path) == 0)
			{
#ifdef DEBUG_TASK_FINDER_VMA
			  _stp_dbug(__FUNCTION__, __LINE__,
				    "vm_cb: matched path %s to module (for sec: %s)\n",
				    path, _stp_modules[i]->sections[0].name);
#endif
			  module = _stp_modules[i];
			  /* XXX We really only need to register .dynamic
			     sections, but .absolute exes are also necessary
			     atm. */
			  return stap_add_vma_map_info(tsk->group_leader,
						       addr,
						       addr + length,
						       offset,
						       module);
			}
		}
	}
	return 0;
}

static int _stp_tf_munmap_cb(struct stap_task_finder_target *tgt,
			     struct task_struct *tsk,
			     unsigned long addr,
			     unsigned long length)
{
        /* Unconditionally remove vm map info, ignore if not present. */
	stap_remove_vma_map_info(tsk->group_leader, addr, addr + length, 0);
	return 0;
}

/* Returns absolute address of offset into module/section for given task.
   If tsk == NULL module/section is assumed to be absolute/static already
   (e.g. kernel, kernel-modules and static executables). Returns zero when
   module and section couldn't be found (aren't in memory yet). */
static unsigned long _stp_module_relocate(const char *module,
					  const char *section,
					  unsigned long offset,
					  struct task_struct *tsk)
{
	unsigned long addr_offset;
	unsigned i, j;

	dbug_sym(1, "%s, %s, %lx\n", module, section, offset);

	if (!module || !strcmp(section, "")	/* absolute, unrelocated address */
	    ||_stp_num_modules == 0) {
		return offset;
	}

        addr_offset = 0;
        for (i = 0; i < _stp_num_modules; i++) {
          struct _stp_module *m = _stp_modules[i];
          if (strcmp(module, m->name))
            continue;
          for (j = 0; j < m->num_sections; j++) {
            struct _stp_section *s = &m->sections[j];
            if (!strcmp(section, s->name)) {
              /* mod and sec name match. tsk should match dynamic/static. */
              if (s->static_addr != 0) {
                addr_offset = s->static_addr;
	      } else {
                if (!tsk) { /* static section, not in memory yet? */
		  if (strcmp(".dynamic", section) == 0)
		    _stp_error("internal error, _stp_module_relocate '%s' "
			       "section '%s', should not be tsk dynamic\n",
			       module, section);
		  return 0;
		} else { /* dynamic section, look up through tsk vma. */
		  if (strcmp(".dynamic", s->name) != 0) {
		    _stp_error("internal error, _stp_module_relocate '%s' "
			       "section '%s', should not be tsk dynamic\n",
			       module, section);
		    return 0;
		  }
		  if (stap_find_vma_map_info_user(tsk->group_leader, m,
						  &addr_offset, NULL,
						  NULL) != 0) {
		    return 0;
		  }
		}
	      }
              offset += addr_offset;
              dbug_sym(1, "address=%lx\n", offset);
              return offset;
            }
          }
	}
	return 0;
}

/* Return module owner and, if sec != NULL, fills in closest section
   of the address if found, return NULL otherwise. Fills in rel_addr
   (addr relative to closest section) when given. */
static struct _stp_module *_stp_mod_sec_lookup(unsigned long addr,
					       struct task_struct *task,
					       struct _stp_section **sec,
					       unsigned long *rel_addr)
{
  void *user = NULL;
  unsigned midx = 0;

  // Try vma matching first if task given.
  if (task)
    {
      unsigned long vm_start = 0;
      if (stap_find_vma_map_info(task->group_leader, addr,
				 &vm_start, NULL,
				 NULL, &user) == 0)
	if (user != NULL)
	  {
	    struct _stp_module *m = (struct _stp_module *)user;
	    if (sec)
	      *sec = &m->sections[0]; // dynamic user modules have one section.
	    if (rel_addr)
	      {
		/* XXX .absolute sections really shouldn't be here... */
		if (strcmp(".dynamic", m->sections[0].name) == 0)
		  *rel_addr = addr - vm_start;
		else
		  *rel_addr = addr;
	      }
	    dbug_sym(1, "found section %s in module %s at 0x%lx\n",
		     m->sections[0].name, m->name, vm_start);
	    return m;
	  }
      /* XXX should really not fallthrough, but sometimes current is passed
             when it shouldn't - see probefunc() for example. */
    }

  for (midx = 0; midx < _stp_num_modules; midx++)
    {
      unsigned secidx;
      for (secidx = 0; secidx < _stp_modules[midx]->num_sections; secidx++)
	{
	  unsigned long sec_addr;
	  unsigned long sec_size;
	  sec_addr = _stp_modules[midx]->sections[secidx].static_addr;
	  sec_size = _stp_modules[midx]->sections[secidx].size;
	  if (addr >= sec_addr && addr < sec_addr + sec_size)
            {
	      if (sec)
		*sec = & _stp_modules[midx]->sections[secidx];
	      if (rel_addr)
		*rel_addr = addr - sec_addr;
	      return _stp_modules[midx];
	    }
	}
      }
  return NULL;
}


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
	unsigned long rel_addr = 0;

	m = _stp_mod_sec_lookup(addr, task, &sec, &rel_addr);
        if (unlikely (m == NULL || sec == NULL))
          return NULL;
        
        /* NB: relativize the address to the section. */
        addr = rel_addr;
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
			  notes_addr = _stp_module_relocate("kernel",
					 "_stext", m->build_id_offset, NULL);
			  base_addr = _stp_module_relocate("kernel",
							   "_stext", 0, NULL);
                    } else {
			  notes_addr = m->notes_sect + m->build_id_offset;
			  base_addr = m->notes_sect;
		    }

		    /* build-id note payload start address */
                    /* XXX: But see https://bugzilla.redhat.com/show_bug.cgi?id=465872;
                       dwfl_module_build_id was not intended to return the end address. */
		    notes_addr -= m->build_id_len;

		    if (notes_addr <= base_addr)  /* shouldn't happen */
			 continue;
                    for (j=0; j<m->build_id_len; j++) {
                            /* Use set_fs / get_user to access
                             conceivably invalid addresses.  If
                             loc2c-runtime.h were more easily usable,
                             a deref() loop could do it too. */
                            mm_segment_t oldfs = get_fs();
                            int rc;
                            unsigned char theory, practice;

                            set_fs(KERNEL_DS);
                            rc = get_user(theory,((unsigned char*) &m->build_id_bits[j]));
                            rc = get_user(practice,((unsigned char*) (void*) (notes_addr+j)));
                            set_fs(oldfs);

                            if (rc || theory != practice) {
                                    const char *basename;
                                    basename = strrchr(m->path, '/');
                                    if (basename)
                                            basename++;
                                    else
                                            basename = m->path;
                                    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
                                    _stp_error ("Build-id mismatch: \"%s\" vs. \"%s\" byte %d (0x%02x vs 0x%02x)\n",
                                                m->name, basename, j, theory, practice);
                                    return 1;
#else
                                    /* This branch is a surrogate for kernels
                                     * affected by Fedora bug #465873. */
                                    _stp_warn (KERN_WARNING
                                               "Build-id mismatch: \"%s\" vs. \"%s\" byte %d (0x%02x vs 0x%02x)\n",
                                               m->name, basename, j, theory, practice);
#endif
                                    break;
                            } /* end mismatch */
		    } /* end per-byte check loop */
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
	const char *modname = 0;
	const char *name = 0;
	unsigned long offset = 0;
        unsigned long size = 0;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL, NULL);

	_stp_printf("%p", (int64_t) address);

	if (name) {
		if (modname && *modname)
			_stp_printf(" : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_printf(" : %s+%#lx/%#lx", name, offset, size);
	}
}

/** Print an user space address from a specific task symbolically.
 * @param address The address to lookup.
 * @param task The address to lookup.
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time.
 */

static void _stp_usymbol_print(unsigned long address, struct task_struct *task)
{
	const char *modname = 0;
	const char *name = 0;
	unsigned long offset = 0;
        unsigned long size = 0;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL,
                                    task);

	_stp_printf("%p", (int64_t) address);

	if (name) {
		if (modname && *modname)
			_stp_printf(" : %s+%#lx/%#lx [%s]", name, offset, size, modname);
		else
			_stp_printf(" : %s+%#lx/%#lx", name, offset, size);
	}
}

/* Like _stp_symbol_print, except only print if the address is a valid function address */
static int _stp_func_print(unsigned long address, int verbose, int exact,
                           struct task_struct *task)
{
	const char *modname;
	const char *name;
	unsigned long offset, size;
	char *exstr;

	if (exact)
		exstr = "";
	else
		exstr = " (inexact)";

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL,
				task);

	if (name) {
		switch (verbose) {
		case SYM_VERBOSE_FULL:
			if (modname && *modname)
				_stp_printf(" %p : %s+%#lx/%#lx [%s]%s\n",
					(int64_t) address, name, offset,
					size, modname, exstr);
			else
				_stp_printf(" %p : %s+%#lx/%#lx%s\n",
					(int64_t) address, name, offset, size,
					exstr);
			break;
		case SYM_VERBOSE_BRIEF:
			_stp_printf("%s+%#lx\n", name, offset);
			break;
		case SYM_VERBOSE_NO:
		default:
			_stp_printf("%p ", (int64_t) address);
		}
		return 1;
	} else if (verbose == SYM_VERBOSE_BRIEF)
		_stp_printf("%p\n", (int64_t) address);
	return 0;
}

/** Puts symbolic information of an address in a string.
 * @param src The string to fill in.
 * @param len The length of the given src string.
 * @param address The address to lookup.
 * @param add_mod Whether to include module name information if found.
 */

static void _stp_symbol_snprint(char *str, size_t len, unsigned long address,
			 struct task_struct *task, int add_mod)
{
	const char *modname;
	const char *name;
	unsigned long offset, size;

	name = _stp_kallsyms_lookup(address, &size, &offset, &modname, NULL,
				    task);
	if (name) {
		if (add_mod && modname && *modname)
			_stp_snprintf(str, len, "%s %s+%#lx/%#lx",
				      name, modname, offset, size);
		else
			strlcpy(str, name, len);
	} else
		_stp_snprintf(str, len, "%p", (int64_t) address);
}

/** @} */


/** @file sym.c
 * @addtogroup sym Symbolic Functions
 * Symbolic Lookup Functions
 * @{
 */
static void _stp_sym_init(void)
{
        // NB: it's too "early" to make this conditional on STP_NEED_VMA_TRACKER,
        // since we're #included at the top of the generated module, before any
        // tapset-induced #define's.
#if defined(CONFIG_UTRACE)
	static int initialized = 0;
        static struct stap_task_finder_target vmcb = {
                // NB: no .pid, no .procname filters here.
                // This means that we get a system-wide mmap monitoring
                // widget while the script is running. (The
                // system-wideness may be restricted by stap -c or
                // -x.)  But this seems to be necessary if we want to
                // to stack tracebacks through arbitrary shared libraries.
                //
                // XXX: There may be an optimization opportunity
                // for executables (for which the main task-finder
                // callback should be sufficient).
                .pid = 0,
                .procname = NULL,
                .callback = &_stp_tf_exec_cb,
                .mmap_callback = &_stp_tf_mmap_cb,
                .munmap_callback = &_stp_tf_munmap_cb,
                .mprotect_callback = NULL
        };
	if (! initialized) {
                int rc;
		__stp_tf_vma_initialize();
                rc = stap_register_task_finder_target (& vmcb);
#ifdef DEBUG_TASK_FINDER_VMA
                _stp_dbug(__FUNCTION__, __LINE__, "registered vmcb");
#endif
                if (rc != 0)
                  _stp_error("Couldn't register task finder target: %d\n", rc);
                else
                  initialized = 1;
	}
#endif
}


#endif /* _STP_SYM_C_ */
