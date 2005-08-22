#define STP_RELAYFS
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_io2");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_getuid (struct kprobe *p, struct pt_regs *regs)
{
  /* print 100 chars */
  _stp_printf ("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
  _stp_print_flush();
  return 0;
}

static int inst_sys_getgid (struct kprobe *p, struct pt_regs *regs)
{
  /* print 100 chars */
  _stp_print_cstr ("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
  _stp_print_flush();
  return 0;
}

static struct kprobe kp[] = {
  {
    .addr = "sys_getuid",
    .pre_handler = inst_sys_getuid
  },
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid
  }
};

#define NUM_KPROBES (sizeof(kp)/sizeof(struct kprobe))

int probe_start(void)
{
  return _stp_register_kprobes (kp, NUM_KPROBES);
}

void probe_exit (void)
{
  _stp_unregister_kprobes (kp, NUM_KPROBES); 
}
