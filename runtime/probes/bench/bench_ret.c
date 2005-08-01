#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1
#define USE_RET_PROBES

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_ret");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_getuid (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_getgid_ret (struct kretprobe_instance *ri, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_getgid (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct kretprobe kpr[] = {
  {
    .kp.addr = "sys_getuid",
    .handler = inst_sys_getuid
  },
  {
    .kp.addr = "sys_getgid",
    .handler = inst_sys_getgid_ret
  }
};

static struct kprobe kp[] = {
  {
    .addr = "sys_getgid",
    .pre_handler = inst_sys_getgid
  }
};


#define NUM_KPROBES (sizeof(kpr)/sizeof(struct kretprobe))

int probe_start(void)
{
  int ret = _stp_register_kretprobes (kpr, NUM_KPROBES);
  if (ret >= 0) {
    if ((ret = _stp_register_kprobes (kp, 1)) < 0)
      _stp_unregister_kretprobes (kpr, NUM_KPROBES);
  }
  return ret;
}

void probe_exit (void)
{
  _stp_unregister_kretprobes (kpr, NUM_KPROBES); 
  _stp_unregister_kprobes (kp, 1); 
}
