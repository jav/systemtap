#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1
#include "runtime.h"

#include "counter.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: count2");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");

Counter opens;
Counter reads;
Counter writes;
Counter read_bytes;
Counter write_bytes;

asmlinkage long inst_sys_open (const char __user * filename, int flags, int mode)
{
  _stp_counter_add (opens, 1);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_counter_add (reads, 1);
  _stp_counter_add (read_bytes, count);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_counter_add (writes, 1);
  _stp_counter_add (write_bytes, count);
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

int probe_start(void)
{
  opens = _stp_counter_init();
  reads = _stp_counter_init();
  writes = _stp_counter_init();
  read_bytes = _stp_counter_init();
  write_bytes = _stp_counter_init();

  return _stp_register_jprobes (stp_probes, MAX_STP_ROUTINE);
}

static void probe_exit (void)
{
  int i;

  _stp_unregister_jprobes (stp_probes, MAX_STP_ROUTINE);

  _stp_printf ("open calls: %lld\n", _stp_counter_get(opens, 0));
  _stp_printf ("read calls: %lld\n", _stp_counter_get(reads, 0));
  _stp_printf ("read bytes: %lld\n", _stp_counter_get(read_bytes, 0));
  _stp_printf ("write calls: %lld\n", _stp_counter_get(writes, 0));
  _stp_printf ("write bytes: %lld\n", _stp_counter_get(write_bytes, 0));
  
  _stp_print_flush();
}
