/* -*- linux-c -*- 
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _KPROBE_COMMON_H_
#define _KPROBE_COMMON_H_

struct stap_dwarf_kprobe {
  union { struct kprobe kp; struct kretprobe krp; } u;
  #ifdef __ia64__
  struct kprobe dummy;
  #endif
};

#endif /* _KPROBE_COMMON_H_ */
