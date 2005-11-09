#define STP_NUM_STRINGS 1
#include "runtime.h"

#define VALUE_TYPE INT64
#define KEY1_TYPE STRING
#include "map-gen.c"

#define VALUE_TYPE STAT
#define KEY1_TYPE STRING
#include "map-gen.c"

#include "map.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: test4");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");


MAP opens, reads, writes;

asmlinkage long inst_sys_open (const char __user * filename, int flags, int mode)
{
  _stp_map_add_si (opens, current->comm, 1);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_map_add_sx (reads, current->comm, count);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_map_add_sx (writes, current->comm, count);
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
  opens = _stp_map_new_si (1000);
  reads = _stp_map_new_sx (1000, HIST_LOG, 12);
  writes = _stp_map_new_sx (1000, HIST_LOG, 12);
  return _stp_register_jprobes (stp_probes, MAX_STP_ROUTINE);
}

void probe_exit (void)
{
  _stp_unregister_jprobes (stp_probes, MAX_STP_ROUTINE);

  _stp_map_print (opens,"%d opens by process \"%1s\"");
  _stp_map_print (reads,"reads by process \"%1s\": %C.  Total bytes=%S.  Average: %A\n%H");
  _stp_map_print (writes,"writes by process \"%1s\": %C.  Total bytes=%S.  Average: %A\n%H");
  _stp_printf("\nDropped %d packets\n", atomic_read(&_stp_transport_failures));
  _stp_print_flush();
  _stp_map_del (opens);
  _stp_map_del (reads);
  _stp_map_del (writes);
}
