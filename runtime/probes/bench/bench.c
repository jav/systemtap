#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: bench");
MODULE_AUTHOR("Martin Hunt");

asmlinkage ssize_t inst_sys_getgid (unsigned int fd, const char __user * buf, size_t count)
{
  jprobe_return();
  return 0;
}

static int inst_sys_getuid (struct kprobe *p, struct pt_regs *regs)
{
  return 0;
}

static struct jprobe jp[] = {
  {
    .kp.addr = (kprobe_opcode_t *)"sys_getgid",
    .entry = (kprobe_opcode_t *) inst_sys_getgid
  },
};

static struct kprobe kp[] = {
  {
    .addr = "sys_getuid",
    .pre_handler = inst_sys_getuid
  }
};

#define NUM_JPROBES (sizeof(jp)/sizeof(struct jprobe))
#define NUM_KPROBES (sizeof(kp)/sizeof(struct kprobe))

int probe_start(void)
{
  int ret = _stp_register_jprobes (jp, NUM_JPROBES);
  if (ret >= 0)
    if ((ret = _stp_register_kprobes (kp, NUM_KPROBES)) < 0)
      _stp_unregister_jprobes (jp, NUM_JPROBES);
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (jp, NUM_JPROBES); 
  _stp_unregister_kprobes (kp, NUM_KPROBES); 
}
