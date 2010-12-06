/* -*- linux-c -*- 
 * kprobe Functions
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _KPROBE_COMMON_C_
#define _KPROBE_COMMON_C_

#ifdef KPROBES_TASK_FINDER

/* The task_finder_callback */
static int stap_kprobe_process_found (struct stap_task_finder_target *finder, struct task_struct *tsk, int register_p, int process_p) {
  struct stap_dwarf_probe *p = container_of(finder, struct stap_dwarf_probe, finder);
  unsigned short sdt_semaphore = 0;
  if (! process_p) return 0; /* ignore threads */
  #ifdef DEBUG_TASK_FINDER_VMA
  _stp_dbug (__FUNCTION__,__LINE__, "%cproc pid %d stf %p %p path %s\n", register_p?'+':'-', tsk->tgid, finder, p, p->pathname);
  #endif
  p->tsk = tsk;
  p->sdt_sem_address = p->sdt_sem_offset;
  if (get_user (sdt_semaphore, (unsigned short __user *) p->sdt_sem_address) == 0) {
    sdt_semaphore ++;
    #ifdef DEBUG_UPROBES
    _stp_dbug (__FUNCTION__,__LINE__, "+semaphore %#x @ %#lx\n", sdt_semaphore, p->sdt_sem_address);
    #endif
    put_user (sdt_semaphore, (unsigned short __user *) p->sdt_sem_address);
  }
  return 0;
}

/* The task_finder_mmap_callback */
static int stap_kprobe_mmap_found (struct stap_task_finder_target *finder, struct task_struct *tsk, char *path, struct dentry *dentry, unsigned long addr, unsigned long length, unsigned long offset, unsigned long vm_flags) {
  struct stap_dwarf_probe *p = container_of(finder, struct stap_dwarf_probe, finder);
  int rc = 0;
  if (path == NULL || strcmp (path, p->pathname)) return 0;
  if (p->sdt_sem_offset && p->sdt_sem_address == 0) {
    p->tsk = tsk;
    if (vm_flags & VM_EXECUTABLE) {
      p->sdt_sem_address = addr + p->sdt_sem_offset;
    }
    else {
      p->sdt_sem_address = (addr - offset) + p->sdt_sem_offset;
    }
  }
  if (p->sdt_sem_address && (vm_flags & VM_WRITE)) {
    unsigned short sdt_semaphore = 0;
    if (get_user (sdt_semaphore, (unsigned short __user *) p->sdt_sem_address) == 0) {
      sdt_semaphore ++;
      #ifdef DEBUG_UPROBES
      _stp_dbug (__FUNCTION__,__LINE__, "+semaphore %#x @ %#lx\n", sdt_semaphore, p->sdt_sem_address);
      #endif
      put_user (sdt_semaphore, (unsigned short __user *) p->sdt_sem_address);
    }
  }
  return 0;
}

#endif /* KPROBES_TASK_FINDER */

#endif /* _KPROBE_COMMON_C_ */
