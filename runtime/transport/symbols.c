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


static void systemtap_module_refresh(void);


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

  _stp_kmodule_update_address(msg.module, msg.reloc, msg.address);
}



static int _stp_module_notifier (struct notifier_block * nb,
                                 unsigned long val, void *data)
{
        struct module *mod = data;

        if (val == MODULE_STATE_COMING) {
                /* A module is arriving.  Register all of its section
                   addresses, as though staprun sent us a bunch of
                   STP_RELOCATE messages.  Now ... where did the
                   fishie go?  It's in mod->sect_attrs, but the type
                   declaration is private to kernel/module.c.  It's in
                   the load_info, but we can't get there from here.
                   It's in sysfs, but one'd have to maneuver through
                   mod->mkobj etc, or consult userspace; not cool.

                   So we cheat.  It's under the sofa.  */

#ifndef STAPCONF_GRSECURITY
                _stp_kmodule_update_address(mod->name, ".text",
                                            (unsigned long)mod->module_core);
                _stp_kmodule_update_address(mod->name, ".init.text",
                                            (unsigned long)mod->module_init);
#else
                _stp_kmodule_update_address(mod->name, ".text",
                                            (unsigned long)mod->module_core_rx);
                _stp_kmodule_update_address(mod->name, ".init.text",
                                            (unsigned long)mod->module_init_rx);
		/* XXX: also: module_*_rw for .data? */
#endif
                /* _stp_kmodule_update_address(mod->name,
                                               ".note.gnu.build-id", ??); */
        }
        else if (val == MODULE_STATE_LIVE) {
                /* The init section(s) may have been unloaded. */
                _stp_kmodule_update_address(mod->name, ".init.text", 0);
        }
        else if (val == MODULE_STATE_GOING) {
                /* Unregister all sections. */
                _stp_kmodule_update_address(mod->name, NULL, 0);
        }

        /* Give the probes a chance to update themselves. */
        /* Proper kprobes support for this appears to be relatively
           recent.  Example prerequisite commits: 0deddf436a f24659d9 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
        systemtap_module_refresh();
#endif

        return NOTIFY_DONE;
}


#endif /* _STP_SYMBOLS_C_ */
