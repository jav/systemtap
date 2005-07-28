#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_multi");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_getuid1 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_getuid2 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_getgid1 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_getgid2 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_getgid3 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}
static int inst_sys_getgid4 (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct kprobe kp[] = {
  {
    .addr = "sys_getuid",
    .pre_handler = inst_sys_getuid1
  },
  {
    .addr = "sys_getuid",
    .pre_handler = inst_sys_getuid2
  },
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid1
  },
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid2
  },
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid3
  },
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid4
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
