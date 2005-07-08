#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_multi");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_read1 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_read2 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_write1 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_write2 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_write3 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_write4 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct kprobe kp[] = {
  {
    .addr = "sys_read",
    .pre_handler = inst_sys_read1
  },
  {
    .addr = "sys_read",
    .pre_handler = inst_sys_read2
  },
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write1
  },
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write2
  },
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write3
  },
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write4
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
}
