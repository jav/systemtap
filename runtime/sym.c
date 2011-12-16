/* -*- linux-c -*- 
 * Symbolic Lookup Functions
 * Copyright (C) 2005-2011 Red Hat Inc.
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
#include "vma.c"
#include "string.c"
#include <asm/uaccess.h>

#ifdef STAPCONF_PROBE_KERNEL
#include <linux/uaccess.h>
#endif

/* Returns absolute address of offset into kernel module/section.
   Returns zero when module and section couldn't be found
   (aren't in memory yet). */
static unsigned long _stp_kmodule_relocate(const char *module,
					   const char *section,
					   unsigned long offset)
{
  unsigned i, j;

  dbug_sym(1, "%s, %s, %lx\n", module, section, offset);

  /* absolute, unrelocated address */
  if (!module || !strcmp(section, "")
      ||_stp_num_modules == 0) {
    return offset;
  }

  for (i = 0; i < _stp_num_modules; i++) {
    struct _stp_module *m = _stp_modules[i];
    if (strcmp(module, m->name))
      continue;

    for (j = 0; j < m->num_sections; j++) {
      struct _stp_section *s = &m->sections[j];
      if (!strcmp(section, s->name)) {
	/* mod and sec name match. tsk should match dynamic/static. */
	if (s->static_addr != 0) {
	  unsigned long addr = offset + s->static_addr;
	  dbug_sym(1, "address=%lx\n", addr);
	  return addr;
	} else {
	  /* static section, not in memory yet? */
	  dbug_sym(1, "section %s, not in memory yet?", s->name);
	  return 0;
	}
      }
    }
  }

  return 0;
}

static unsigned long _stp_umodule_relocate(const char *path,
					   unsigned long offset,
					   struct task_struct *tsk)
{
  unsigned i;
  unsigned long vm_start = 0;

  dbug_sym(1, "%s, %lx\n", path, offset);

  for (i = 0; i < _stp_num_modules; i++) {
    struct _stp_module *m = _stp_modules[i];

    if (strcmp(path, m->path)
	|| m->num_sections != 1
	|| strcmp(m->sections[0].name, ".dynamic"))
      continue;

    if (stap_find_vma_map_info_user(tsk->group_leader, m,
				    &vm_start, NULL, NULL) == 0) {
      offset += vm_start;
      dbug_sym(1, "address=%lx\n", offset);
      return offset;
    }
  }

  return 0;
}

/* Return (kernel) module owner and, if sec != NULL, fills in closest
   section of the address if found, return NULL otherwise. */
static struct _stp_module *_stp_kmod_sec_lookup(unsigned long addr,
						struct _stp_section **sec)
{
  unsigned midx = 0;

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
	      return _stp_modules[midx];
	    }
	}
      }
  return NULL;
}

/* Return (user) module in which the the given addr falls.  Returns
   NULL when no module can be found that contains the addr.  Fills in
   vm_start (addr where module is mapped in) and (base) name of module
   when given.  Note that user modules always have exactly one section
   (.dynamic or .absolute). */
static struct _stp_module *_stp_umod_lookup(unsigned long addr,
					    struct task_struct *task,
					    const char **name,
					    unsigned long *vm_start,
					    unsigned long *vm_end)
{
  void *user = NULL;
#ifdef CONFIG_COMPAT
        /* Handle 32bit signed values in 64bit longs, chop off top bits. */
        if (test_tsk_thread_flag(task, TIF_32BIT))
          addr &= ((compat_ulong_t) ~0);
#endif
  if (stap_find_vma_map_info(task->group_leader, addr,
			     vm_start, vm_end, name, &user) == 0)
    if (user != NULL)
      {
	struct _stp_module *m = (struct _stp_module *)user;
	dbug_sym(1, "found module %s at 0x%lx\n", m->path,
		 vm_start ? *vm_start : 0);
	return m;
      }
  return NULL;
}

static const char *_stp_kallsyms_lookup(unsigned long addr,
                                        unsigned long *symbolsize,
                                        unsigned long *offset, 
                                        const char **modname, 
                                        /* char ** secname? */
					struct task_struct *task)
{
	struct _stp_module *m = NULL;
	struct _stp_section *sec = NULL;
	struct _stp_symbol *s = NULL;
	unsigned end, begin = 0;
	unsigned long rel_addr = 0;

	if (addr == 0)
	  return NULL;

	if (task)
	  {
	    unsigned long vm_start = 0;
	    unsigned long vm_end = 0;
#ifdef CONFIG_COMPAT
        /* Handle 32bit signed values in 64bit longs, chop off top bits.
           _stp_umod_lookup does the same, but we need it here for the
           binary search on addr below. */
        if (test_tsk_thread_flag(task, TIF_32BIT))
          addr &= ((compat_ulong_t) ~0);
#endif
	    m = _stp_umod_lookup(addr, task, modname, &vm_start, &vm_end);
	    if (m)
	      {
		sec = &m->sections[0];
		/* XXX .absolute sections really shouldn't be here... */
		if (strcmp(".dynamic", m->sections[0].name) == 0)
		  rel_addr = addr - vm_start;
		else
		  rel_addr = addr;
	      }
	    if (modname && *modname)
	      {
		/* In case no symbol is found, fill in based on module. */
		if (offset)
		  *offset = addr - vm_start;
		if (symbolsize)
		  *symbolsize = vm_end - vm_start;
	      }
	  }
	else
	  {
	    m = _stp_kmod_sec_lookup(addr, &sec);
	    if (m)
	      {
	        rel_addr = addr - sec->static_addr;
		if (modname)
		  *modname = m->name;
	      }
	  }

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
                /* We could also pass sec->name here. */
		if (symbolsize) {
			if ((begin + 1) < sec->num_symbols)
				*symbolsize = sec->symbols[begin + 1].addr - s->addr;
			else
				*symbolsize = 0;
			// NB: This is only a heuristic.  Sometimes there are large
			// gaps between text areas of modules.
		}
		return s->symbol;
	}
	return NULL;
}

static int _stp_build_id_check (struct _stp_module *m, unsigned long notes_addr, int user_module)
{
  int j;
  for (j=0; j<m->build_id_len; j++) {
    /* Use set_fs / get_user to access
       conceivably invalid addresses.  If
       loc2c-runtime.h were more easily usable,
       a deref() loop could do it too. */
    mm_segment_t oldfs = get_fs();
    int rc;
    unsigned char theory, practice;

#ifdef STAPCONF_PROBE_KERNEL
    if (!user_module)
      {
        theory = m->build_id_bits[j];
        rc=probe_kernel_read(&practice, (void*)(notes_addr+j), 1);
      }
    else
#endif
      {
        set_fs (user_module ? USER_DS : KERNEL_DS);
        theory = m->build_id_bits[j];
        rc = get_user(practice,((unsigned char*) (void*) (notes_addr+j)));
        set_fs(oldfs);
      }

    if (rc || (theory != practice)) {
      const char *basename;
      basename = strrchr(m->path, '/');
      if (basename)
	basename++;
      else
	basename = m->path;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
      _stp_error ("Build-id mismatch: \"%s\" vs. \"%s\" byte %d (0x%02x vs 0x%02x) address %#lx rc %d\n",
		  m->name, basename, j, theory, practice, notes_addr, rc);
      return 1;
#else
      /* This branch is a surrogate for kernels
       * affected by Fedora bug #465873. */
      _stp_warn (KERN_WARNING
		 "Build-id mismatch: \"%s\" vs. \"%s\" byte %d (0x%02x vs 0x%02x) rc %d\n",
		 m->name, basename, j, theory, practice, rc);
#endif
      break;
    } /* end mismatch */
  } /* end per-byte check loop */
  return 0;
}


/* Validate module/kernel based on build-id (if present)
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

#ifdef STP_NO_BUILDID_CHECK
  return 0;
#endif

  for (i = 0; i < _stp_num_modules; i++)
    {
      m = _stp_modules[i];
      if (m->build_id_len > 0 && m->notes_sect != 0) {
          dbug_sym(1, "build-id validation [%s]\n", m->name);

          /* notes end address */
          if (!strcmp(m->name, "kernel")) {
              notes_addr = _stp_kmodule_relocate("kernel",
                  "_stext", m->build_id_offset);
              base_addr = _stp_kmodule_relocate("kernel",
                  "_stext", 0);
          } else {
              notes_addr = m->notes_sect + m->build_id_offset;
              base_addr = m->notes_sect;
          }

          if (notes_addr <= base_addr) { /* shouldn't happen */
              _stp_warn ("build-id address %lx < base %lx\n",
                  notes_addr, base_addr);
              continue;
          }
          return _stp_build_id_check (m, notes_addr, 0);
      } /* end checking */
    } /* end loop */
  return 0;
}



/* Iterate over _stp_modules, looking for a kernel module of given
   name.  Run build-id checking for it.  Return 0 on ok. */
static int _stp_kmodule_check (const char *name)
{
  struct _stp_module *m = NULL;
  unsigned long notes_addr, base_addr;
  unsigned i,j;

#ifdef STP_NO_BUILDID_CHECK
  return 0;
#endif

  for (i = 0; i < _stp_num_modules; i++)
    {
      m = _stp_modules[i];
      if (strcmp (name, m->name)) continue;

      if (m->build_id_len > 0 && m->notes_sect != 0) {
          dbug_sym(1, "build-id validation [%s]\n", m->name);

          /* notes end address */
          notes_addr = m->notes_sect + m->build_id_offset;
          base_addr = m->notes_sect;

          if (notes_addr <= base_addr) { /* shouldn't happen */
              _stp_warn ("build-id address %lx < base %lx\n",
                  notes_addr, base_addr);
              continue;
          }
          return _stp_build_id_check (m, notes_addr, 0);
      } /* end checking */
    } /* end loop */

  return 0; /* name not found */
}



/* Validate user module based on build-id (if present) */
static int _stp_usermodule_check(struct task_struct *tsk, const char *path_name, unsigned long addr)
{
  struct _stp_module *m = NULL;
  unsigned long notes_addr;
  unsigned i, j;
  unsigned char practice_id_bits[MAXSTRINGLEN];
  unsigned long vm_end = 0;

#ifdef STP_NO_BUILDID_CHECK
  return 0;
#endif

  for (i = 0; i < _stp_num_modules; i++)
    {
      m = _stp_modules[i];
      if (strcmp(path_name, _stp_modules[i]->path) != 0)
	continue;
      if (m->build_id_len > 0) {
	int ret, build_id_len;

	notes_addr = addr + m->build_id_offset /* + m->module_base */;

        dbug_sym(1, "build-id validation [%s] address=%#lx build_id_offset=%#lx\n",
            m->name, addr, m->build_id_offset);

	if (notes_addr <= addr) {
	  _stp_warn ("build-id address %lx < base %lx\n",
		     notes_addr, addr);
	  continue;
	}
	return _stp_build_id_check (m, notes_addr, 1);
      }
    }

  return 0;
}


/** Prints an address based on the _STP_SYM flags.
 * @param address The address to lookup.
 * @param task The address to lookup (if NULL lookup kernel/module address).
 * @note Symbolic lookups should not normally be done within
 * a probe because it is too time-consuming. Use at module exit time. */
static int _stp_snprint_addr(char *str, size_t len, unsigned long address,
			     int flags, struct task_struct *task)
{
  const char *modname = NULL;
  const char *name = NULL;
  unsigned long offset = 0, size = 0;
  char *exstr, *poststr, *prestr;

  prestr = (flags & _STP_SYM_PRE_SPACE) ? " " : "";
  exstr = (((flags & _STP_SYM_INEXACT) && (flags & _STP_SYM_SYMBOL))
	   ? " (inexact)" : "");
  if (flags & _STP_SYM_POST_SPACE)
    poststr = " ";
  else if (flags & _STP_SYM_NEWLINE)
    poststr = "\n";
  else
    poststr = "";

  if (flags & (_STP_SYM_SYMBOL | _STP_SYM_MODULE)) {
    name = _stp_kallsyms_lookup(address, &size, &offset, &modname, task);
    if (name && name[0] == '.')
      name++;
  }

  if (modname && (flags & _STP_SYM_MODULE_BASENAME)) {
     char *slash = strrchr (modname, '/');
     if (slash)
        modname = slash+1;
  }

  if (name && (flags & _STP_SYM_SYMBOL)) {
    if ((flags & _STP_SYM_MODULE) && modname && *modname) {
      if (flags & _STP_SYM_OFFSET) {
	if (flags & _STP_SYM_SIZE) {
	  /* symbol, module, offset and size. */
	  if (flags & _STP_SYM_HEX_SYMBOL)
	    return _stp_snprintf(str, len, "%s%p : %s+%#lx/%#lx [%s]%s%s",
				 prestr, (int64_t) address,
				 name, offset, size, modname,
				 exstr, poststr);
	  else
	    return _stp_snprintf(str, len, "%s%s+%#lx/%#lx [%s]%s%s",
				 prestr, name, offset, size,
				 modname, exstr, poststr);
	} else {
	  /* symbol, module, offset. */
	  if (flags & _STP_SYM_HEX_SYMBOL)
	    return _stp_snprintf(str, len, "%s%p : %s+%#lx [%s]%s%s",
				 prestr, (int64_t) address,
				 name, offset, modname,
				 exstr, poststr);
	  else
	    return _stp_snprintf(str, len, "%s%s+%#lx [%s]%s%s",
				 prestr, name, offset,
				 modname, exstr, poststr);
	}
      } else {
	/* symbol plus module */
	if (flags & _STP_SYM_HEX_SYMBOL)
	  return _stp_snprintf(str, len, "%s%p : %s [%s]%s%s", prestr,
			       (int64_t) address, name, modname,
			       exstr, poststr);
	else
	  return _stp_snprintf(str, len, "%s%s [%s]%s%s", prestr, name,
			       modname, exstr, poststr);
      }
    } else if (flags & _STP_SYM_OFFSET) {
      if (flags & _STP_SYM_SIZE) {
	/* symbol name, offset + size, no module name */
	if (flags & _STP_SYM_HEX_SYMBOL)
	  return _stp_snprintf(str, len, "%s%p : %s+%#lx/%#lx%s%s", prestr,
			       (int64_t) address, name, offset,
			       size, exstr, poststr);
	else
	  return _stp_snprintf(str, len, "%s%s+%#lx/%#lx%s%s", prestr, name,
			       offset, size, exstr, poststr);
      } else {
	/* symbol name, offset, no module name */
	if (flags & _STP_SYM_HEX_SYMBOL)
	  return _stp_snprintf(str, len, "%s%p : %s+%#lx%s%s", prestr,
			       (int64_t) address, name, offset,
			       exstr, poststr);
	else
	  return _stp_snprintf(str, len, "%s%s+%#lx%s%s", prestr, name,
			       offset, exstr, poststr);
      }
    } else {
      /* symbol name only */
      if (flags & _STP_SYM_HEX_SYMBOL)
	return _stp_snprintf(str, len, "%s%p : %s%s%s", prestr,
			     (int64_t) address, name, exstr, poststr);
      else
	return _stp_snprintf(str, len, "%s%s%s%s", prestr, name,
			     exstr, poststr);
    }
  } else {
    /* no symbol name */
    if (modname && *modname && (flags & _STP_SYM_MODULE)) {
      if (flags & _STP_SYM_OFFSET) {
        if (flags & _STP_SYM_SIZE) {
          /* hex address, module name, offset + size */
          return _stp_snprintf(str, len, "%s%p [%s+%#lx/%#lx]%s%s", prestr,
			       (int64_t) address, modname, offset,
			       size, exstr, poststr);
        } else {
          /* hex address, module name, offset */
	  return _stp_snprintf(str, len, "%s%p [%s+%#lx]%s%s", prestr,
			       (int64_t) address, modname, offset,
			       exstr, poststr);
        }
      } else {
	/* hex address, module name */
        return _stp_snprintf(str, len, "%s%p [%s]%s%s", prestr,
			     (int64_t) address, modname, exstr, poststr);
      }
#ifdef STAPCONF_MODULE_TEXT_ADDRESS
    } if ((flags & _STP_SYM_MODULE) && ! task) {
      /* No symbol, nor module name, but user really wants one, do one
	 last try. */
      struct module *ko;
      preempt_disable();
      ko = __module_text_address (address);
      if (ko && ko->name)
	{
	  /* hex address, module name */
	  int ret = _stp_snprintf(str, len, "%s%p [%s]%s%s", prestr,
				  (int64_t) address, ko->name, exstr, poststr);
	  preempt_enable_no_resched();
	  return ret;
        }
      preempt_enable_no_resched();
      /* no names, hex only */
      return _stp_snprintf(str, len, "%s%p%s%s", prestr,
			   (int64_t) address, exstr, poststr);
#endif
    } else {
      /* no names, hex only */
      return _stp_snprintf(str, len, "%s%p%s%s", prestr,
			   (int64_t) address, exstr, poststr);
    }
  }
}

static void _stp_print_addr(unsigned long address, int flags,
			    struct task_struct *task)
{
  _stp_snprint_addr(NULL, 0, address, flags, task);
}

/** @} */



/* Update the given module/section's offset value.  Assume that there
   is no need for locking or for super performance.  NB: this is only
   for kernel modules, which exist singly at run time.  User-space
   modules (executables, shared libraries) exist at different
   addresses in different processes, so are tracked in the
   _stp_tf_vma_map. */
static void _stp_kmodule_update_address(const char* module,
                                        const char* reloc, /* NULL="all" */
                                        unsigned long address)
{
  unsigned mi, si;
        
  for (mi=0; mi<_stp_num_modules; mi++)
    {
      const char *note_sectname = ".note.gnu.build-id";
      if (strcmp (_stp_modules[mi]->name, module))
        continue;

      if (reloc && !strcmp (note_sectname, reloc)) {
        dbug_sym(1, "module %s special section %s address %#lx\n",
                 _stp_modules[mi]->name,
                 note_sectname,
                 address);
        _stp_modules[mi]->notes_sect = address;   /* cache this particular address  */
      }

      for (si=0; si<_stp_modules[mi]->num_sections; si++)
        {
          if (reloc && strcmp (_stp_modules[mi]->sections[si].name, reloc))
            continue;
          else
            {
              dbug_sym(1, "module %s section %s address %#lx\n",
                       _stp_modules[mi]->name,
                       _stp_modules[mi]->sections[si].name,
                       address);
              _stp_modules[mi]->sections[si].static_addr = address;

              if (reloc) break;
              else continue; /* wildcarded - will have more hits */
            }
        } /* loop over sections */
    } /* loop over modules */
}



#endif /* _STP_SYM_C_ */
