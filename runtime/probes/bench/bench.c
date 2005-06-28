#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench");
MODULE_AUTHOR("Martin Hunt");

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  jprobe_return();
  return 0;
}

static int inst_sys_read (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct jprobe jp[] = {
  {
    .kp.addr = (kprobe_opcode_t *)"sys_write",
    .entry = (kprobe_opcode_t *) inst_sys_write
  },
};

static struct kprobe kp[] = {
  {
    .addr = "sys_read",
    .pre_handler = inst_sys_read
  }
};

#define NUM_JPROBES (sizeof(jp)/sizeof(struct jprobe))
#define NUM_KPROBES (sizeof(kp)/sizeof(struct kprobe))

int init_module(void)
{
  int ret;

  TRANSPORT_OPEN;
  
  ret = _stp_register_jprobes (jp, NUM_JPROBES);
  if (ret >= 0)
    ret = _stp_register_kprobes (kp, NUM_KPROBES);
  
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (jp, NUM_JPROBES); 
  _stp_unregister_kprobes (kp, NUM_KPROBES); 
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");
