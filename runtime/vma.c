/* -*- linux-c -*- 
 * VMA tracking and lookup functions.
 *
 * Copyright (C) 2005-2010 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_VMA_C_
#define _STP_VMA_C_

#include "sym.h"
#include "string.c"
#include "task_finder_vma.c"

/* exec callback, will drop all vma maps for a process that disappears. */
static int _stp_vma_exec_cb(struct stap_task_finder_target *tgt,
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

/* mmap callback, will match new vma with _stp_module or register vma name. */
static int _stp_vma_mmap_cb(struct stap_task_finder_target *tgt,
			    struct task_struct *tsk,
			    char *path, struct dentry *dentry,
			    unsigned long addr,
			    unsigned long length,
			    unsigned long offset,
			    unsigned long vm_flags)
{
	int i, res;
	struct _stp_module *module = NULL;

#ifdef DEBUG_TASK_FINDER_VMA
	_stp_dbug(__FUNCTION__, __LINE__,
		  "mmap_cb: tsk %d:%d path %s, addr 0x%08lx, length 0x%08lx, offset 0x%lx, flags 0x%lx\n",
		  tsk->pid, tsk->tgid, path, addr, length, offset, vm_flags);
#endif
	// We are only interested in the first load of the whole module that
	// is executable. We register whether or not we know the module,
	// so we can later lookup the name given an address for this task.
	if (path != NULL && offset == 0 && (vm_flags & VM_EXEC)) {
		for (i = 0; i < _stp_num_modules; i++) {
			if (strcmp(path, _stp_modules[i]->path) == 0)
			{
#ifdef DEBUG_TASK_FINDER_VMA
			  _stp_dbug(__FUNCTION__, __LINE__,
				    "vm_cb: matched path %s to module (sec: %s)\n",
				    path, _stp_modules[i]->sections[0].name);
#endif
			  module = _stp_modules[i];
			  /* XXX We really only need to register .dynamic
			     sections, but .absolute exes are also necessary
			     atm. */
			  res = stap_add_vma_map_info(tsk->group_leader,
						      addr, addr + length,
						      dentry, module);
			  /* Warn, but don't error out. */
			  if (res != 0)
				_stp_warn ("Couldn't register module '%s' for pid %d\n", dentry->d_name.name, tsk->group_leader->pid);
			  return 0;
			}
		}

		/* None of the tracked modules matched, register without,
		 * to make sure we can lookup the name later. Ignore errors,
		 * we will just report unknown when asked and tables were
		 * full. Restrict to target process when given to preserve
		 * vma_map entry slots. */
		if (_stp_target == 0
		    || _stp_target == tsk->group_leader->pid)
		  {
		    res = stap_add_vma_map_info(tsk->group_leader, addr,
						addr + length, dentry, NULL);
#ifdef DEBUG_TASK_FINDER_VMA
		    _stp_dbug(__FUNCTION__, __LINE__,
			      "registered '%s' for %d (res:%d)\n",
			      dentry->d_name.name, tsk->group_leader->pid,
			      res);
#endif
		  }

	}
	return 0;
}

/* munmap callback, removes vma map info. */
static int _stp_vma_munmap_cb(struct stap_task_finder_target *tgt,
			      struct task_struct *tsk,
			      unsigned long addr,
			      unsigned long length)
{
        /* Unconditionally remove vm map info, ignore if not present. */
	stap_remove_vma_map_info(tsk->group_leader, addr);
	return 0;
}

/* Provides name of the vma that an address is in for a given task,
 * or NULL if not found.
 */
static const char *_stp_vma_module_name(struct task_struct *tsk,
					unsigned long addr)
{
	struct dentry *dentry = NULL;
#ifdef CONFIG_COMPAT
	/* Handle 32bit signed values in 64bit longs, chop off top bits. */
	if (tsk && test_tsk_thread_flag(tsk, TIF_32BIT))
	  addr &= ((compat_ulong_t) ~0);
#endif
	if (stap_find_vma_map_info(tsk->group_leader, addr,
				   NULL, NULL, &dentry, NULL) == 0)
		if (dentry != NULL)
			return dentry->d_name.name;

	return NULL;
}

/* Initializes the vma tracker. */
static int _stp_vma_init(void)
{
        int rc = 0;
#if defined(CONFIG_UTRACE)
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
                .callback = &_stp_vma_exec_cb,
                .mmap_callback = &_stp_vma_mmap_cb,
                .munmap_callback = &_stp_vma_munmap_cb,
                .mprotect_callback = NULL
        };
	stap_initialize_vma_map ();
#ifdef DEBUG_TASK_FINDER_VMA
	_stp_dbug(__FUNCTION__, __LINE__,
		  "registering vmcb (_stap_target: %d)\n", _stp_target);
#endif
	rc = stap_register_task_finder_target (& vmcb);
	if (rc != 0)
		_stp_error("Couldn't register task finder target: %d\n", rc);
#endif
	return rc;
}

#endif /* _STP_VMA_C_ */
