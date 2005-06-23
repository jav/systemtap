#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"

#include "counter.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: count1");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");

Counter opens;
Counter reads;
Counter writes;
Counter sched;
Counter idle;

static int inst_sys_open (struct kprobe *p, struct pt_regs *regs)
{
  _stp_counter_add (opens, 1);
  return 0;
}

static int inst_sys_read (struct kprobe *p, struct pt_regs *regs)
{
  _stp_counter_add (reads, 1);
  return 0;
}

static int inst_sys_write (struct kprobe *p, struct pt_regs *regs)
{
  _stp_counter_add (writes, 1);
  return 0;
}

static int inst_schedule(struct kprobe *p, struct pt_regs *regs)
{
  _stp_counter_add (sched, 1);
  return 0;
}

static int inst_idle_cpu(struct kprobe *p, struct pt_regs *regs)
{
  _stp_counter_add (idle, 1);
  return 0;
}

static struct kprobe stp_probes[] = {
  {
    .addr = "sys_open",
    .pre_handler =  inst_sys_open
  },
  {
    .addr = "sys_read",
    .pre_handler =  inst_sys_read
  },
  {
    .addr = "sys_write",
    .pre_handler =  inst_sys_write
  },
  {
    .addr = "schedule",
    .pre_handler =  inst_schedule
  },
  {
    .addr = "idle_cpu",
    .pre_handler =  inst_idle_cpu
  },
};

#define MAX_STP_ROUTINE (sizeof(stp_probes)/sizeof(struct kprobe))

int init_module(void)
{
  int ret;
  
  TRANSPORT_OPEN;

  opens = _stp_counter_init();
  reads = _stp_counter_init();
  writes = _stp_counter_init();
  sched = _stp_counter_init();
  idle = _stp_counter_init();

  ret = _stp_register_kprobes (stp_probes, MAX_STP_ROUTINE);

  return ret;
}

static void probe_exit (void)
{
  int i;

  _stp_unregister_kprobes (stp_probes, MAX_STP_ROUTINE);

  for_each_cpu(i)
    _stp_printf ("sched calls for cpu %d = %lld\n", i, _stp_counter_get_cpu(sched, i, 0));
  
  _stp_print ("\n\n");

  _stp_printf ("open calls: %lld\n", _stp_counter_get(opens, 0));
  _stp_printf ("read calls: %lld\n", _stp_counter_get(reads, 0));
  _stp_printf ("write calls: %lld\n", _stp_counter_get(writes, 0));
  _stp_printf ("sched calls: %lld\n", _stp_counter_get(sched, 0));
  _stp_printf ("idle calls: %lld\n", _stp_counter_get(idle, 0));
  _stp_print_flush();
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

