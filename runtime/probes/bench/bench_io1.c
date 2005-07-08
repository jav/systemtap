#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_io");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_read (struct kprobe *p, struct pt_regs *regs)
{
  /* print 100 chars */
  _stp_printf ("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
  _stp_print_flush();
  return 0;
}

static int inst_sys_write (struct kprobe *p, struct pt_regs *regs)
{
  /* print 100 chars */
  _stp_print_cstr ("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
  _stp_print_flush();
  return 0;
}

static struct kprobe kp[] = {
  {
    .addr = "sys_read",
    .pre_handler = inst_sys_read
  },
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write
  }
};

#define NUM_KPROBES (sizeof(kp)/sizeof(struct kprobe))

int probe_start(void)
{
  return _stp_register_kprobes (kp, NUM_KPROBES);
}

static void probe_exit (void)
{
  _stp_unregister_kprobes (kp, NUM_KPROBES); 
  _stp_printf("dropped %d packets\n", atomic_read(&_stp_transport_failures));
  _stp_print_flush();
}
