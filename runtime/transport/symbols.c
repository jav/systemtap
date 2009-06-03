/* -*- linux-c -*- 
 * symbols.c - stp symbol and module functions
 *
 * Copyright (C) Red Hat Inc, 2006-2009
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_SYMBOLS_C_
#define _STP_SYMBOLS_C_
#include "../sym.h"

static void _stp_do_relocation(const char __user *buf, size_t count)
{
  struct _stp_msg_relocation msg;
  unsigned mi, si;

  if (sizeof(msg) != count)
    {
      errk ("STP_RELOCATE message size mismatch (%lu vs %lu)\n",
            (long unsigned) sizeof(msg), (long unsigned) count);
      return;
    }

  if (unlikely(copy_from_user (& msg, buf, count)))
    return;

  dbug_sym(2, "relocate (%s %s 0x%lx)\n", msg.module, msg.reloc, (unsigned long) msg.address);

  /* Detect actual kernel load address. */
  if (!strcmp ("kernel", msg.module)
      && !strcmp ("_stext", msg.reloc)) {
    dbug_sym(2, "found kernel _stext load address: 0x%lx\n",
             (unsigned long) msg.address);
    if (_stp_kretprobe_trampoline != (unsigned long) -1)
      _stp_kretprobe_trampoline += (unsigned long) msg.address;
  }

  /* Save the relocation value.  XXX: While keeping the data here is
     fine for the kernel address space ("kernel" and "*.ko" modules),
     it is NOT fine for user-space apps.  They need a separate
     relocation values for each address space, since the same shared
     libraries/executables can be mapped in at different
     addresses.  */

  for (mi=0; mi<_stp_num_modules; mi++)
    {
      if (strcmp (_stp_modules[mi]->name, msg.module))
        continue;

      if (!strcmp (".note.gnu.build-id", msg.reloc)) {
        _stp_modules[mi]->notes_sect = msg.address;   /* cache this particular address  */
      }

      for (si=0; si<_stp_modules[mi]->num_sections; si++)
        {
          if (strcmp (_stp_modules[mi]->sections[si].name, msg.reloc))
            continue;
          else
            {
              _stp_modules[mi]->sections[si].addr = msg.address;
              break;
            }
        } /* loop over sections */
    } /* loop over modules */
}

#endif /* _STP_SYMBOLS_C_ */
