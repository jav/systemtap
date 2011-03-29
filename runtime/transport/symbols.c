/* -*- linux-c -*- 
 * symbols.c - stp symbol and module functions
 *
 * Copyright (C) Red Hat Inc, 2006-2011
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_SYMBOLS_C_
#define _STP_SYMBOLS_C_
#include "../sym.h"

/* PR12612: pre-commit-3abb860f values */

#define STP13_MODULE_NAME_LEN 64
#define STP13_SYMBOL_NAME_LEN 64
struct _stp13_msg_relocation {
        char module[STP13_MODULE_NAME_LEN];
        char reloc[STP13_SYMBOL_NAME_LEN];
        uint64_t address;
};

static void _stp_do_relocation(const char __user *buf, size_t count)
{
  static struct _stp_msg_relocation msg; /* by protocol, never concurrently used */
  static struct _stp13_msg_relocation msg13; /* ditto */
  unsigned mi, si;

  /* PR12612: Let's try to be compatible with systemtap modules being
     compiled by new systemtap, but loaded (staprun'd) by an older
     systemtap runtime.  The only known incompatilibility is that we
     get an older, smaller, relocation message.  So here we accept both
     sizes. */
  if (sizeof(msg) == count) { /* systemtap 1.4+ runtime */
    if (unlikely(copy_from_user (& msg, buf, count)))
            return;
  } else if (sizeof(msg13) == count) { /* systemtap 1.3- runtime */
    if (unlikely(copy_from_user (& msg13, buf, count)))
            return;
#if STP_MODULE_NAME_LEN <= STP13_MODULE_NAME_LEN
#error "STP_MODULE_NAME_LEN should not be smaller than STP13_MODULE_NAME_LEN"
#endif
    strlcpy (msg.module, msg13.module, STP13_MODULE_NAME_LEN);
    strlcpy (msg.reloc, msg13.reloc, STP13_MODULE_NAME_LEN);
    msg.address = msg13.address;
  } else {
      errk ("STP_RELOCATE message size mismatch (%lu or %lu vs %lu)\n",
            (long unsigned) sizeof(msg), (long unsigned) sizeof (msg13), (long unsigned) count);
      return;
  }

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
              _stp_modules[mi]->sections[si].static_addr = msg.address;
              break;
            }
        } /* loop over sections */
    } /* loop over modules */
}

#endif /* _STP_SYMBOLS_C_ */
