#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1
#define USE_RET_PROBES

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench_ret");
MODULE_AUTHOR("Martin Hunt");

static int inst_sys_read (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_write_ret (struct kretprobe_instance *ri, struct pt_regs *regs)
{
  return 0;
}

static int inst_sys_write (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct kretprobe kpr[] = {
  {
    .kp.addr = "sys_read",
    .handler = inst_sys_read
  },
  {
    .kp.addr = "sys_write",
    .handler = inst_sys_write_ret
  }
};

static struct kprobe kp[] = {
  {
    .addr = "sys_write",
    .pre_handler = inst_sys_write
  }
};


#define NUM_KPROBES (sizeof(kpr)/sizeof(struct kretprobe))

int init_module(void)
{
  int ret;

  TRANSPORT_OPEN;
  
  ret = _stp_register_kretprobes (kpr, NUM_KPROBES);
  ret = _stp_register_kprobes (kp, 1);
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_kretprobes (kpr, NUM_KPROBES); 
  _stp_unregister_kprobes (kp, 1); 
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");
