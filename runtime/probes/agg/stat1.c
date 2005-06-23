#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1
#include "runtime.h"
#include "stat.c"
#include "counter.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: stat1");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");


Counter opens;
Stat reads;
Stat writes;

asmlinkage long inst_sys_open (const char __user * filename, int flags, int mode)
{
  _stp_counter_add (opens, 1);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_stat_add (reads, count);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_stat_add (writes, count);
  jprobe_return();
  return 0;
}

static struct jprobe stp_probes[] = {
  {
    .kp.addr = (kprobe_opcode_t *)"sys_open",
    .entry = (kprobe_opcode_t *) inst_sys_open
  },
  {
    .kp.addr = (kprobe_opcode_t *)"sys_read",
    .entry = (kprobe_opcode_t *) inst_sys_read
  },
  {
    .kp.addr = (kprobe_opcode_t *)"sys_write",
    .entry = (kprobe_opcode_t *) inst_sys_write
  },
};

#define MAX_STP_ROUTINE (sizeof(stp_probes)/sizeof(struct jprobe))

int init_module(void)
{
  int ret;
  
  TRANSPORT_OPEN;

  opens = _stp_counter_init();
  reads = _stp_stat_init(HIST_LOG,24);
  writes = _stp_stat_init(HIST_LINEAR,0,1000,50);

  ret = _stp_register_jprobes (stp_probes, MAX_STP_ROUTINE);
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (stp_probes, MAX_STP_ROUTINE);

  _stp_printf ("OPENS: %lld\n", _stp_counter_get(opens, 0));
  _stp_stat_print (reads, "READS: count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 0);
  _stp_stat_print (writes, "WRITES: count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 0);

  _stp_print_flush();
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

