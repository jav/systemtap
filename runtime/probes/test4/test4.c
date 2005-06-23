#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"

#define NEED_INT64_VALS
#define NEED_STAT_VALS

#define KEY1_TYPE STRING
#include "map-keys.c"

#include "map.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: test4");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");


MAP opens, reads, writes;

asmlinkage long inst_sys_open (const char __user * filename, int flags, int mode)
{
  _stp_map_key_str (opens, current->comm);
  _stp_map_add_int64 (opens, 1);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_map_key_str (reads, current->comm);
  _stp_map_add_int64 (reads, count);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_map_key_str (writes, current->comm);
  _stp_map_add_int64 (writes, count);
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

  /* FIXME. Check return values  */
  opens = _stp_map_new_str (1000, INT64);
  reads = _stp_map_new_str (1000, HSTAT_LOG, 8);
  writes = _stp_map_new_str (1000, HSTAT_LOG, 8);

  ret = _stp_register_jprobes (stp_probes, MAX_STP_ROUTINE);

  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (stp_probes, MAX_STP_ROUTINE);

  _stp_map_print (opens,"%d opens by process \"%1s\"");
  _stp_map_print (reads,"reads by process \"%1s\": %C.  Total bytes=%S.  Average: %A\n%H");
  _stp_map_print (writes,"writes by process \"%1s\": %C.  Total bytes=%S.  Average: %A\n%H");

  _stp_map_del (opens);
  _stp_map_del (reads);
  _stp_map_del (writes);
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

