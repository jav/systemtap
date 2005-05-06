#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */

#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include <linux/module.h>
#include <linux/interrupt.h>
#include <net/sock.h>
#include <linux/netlink.h>

#include "runtime.h"
#include "map.c"
#include "probes.c"
#include "stack.c"

MODULE_DESCRIPTION("SystemTap probe: test4");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");


MAP opens, reads, writes, traces;

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
  _stp_map_stat_add (reads, count);
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_map_key_str (writes, current->comm);
  _stp_map_stat_add (writes, count);
  jprobe_return();
  return 0;
}

int inst_show_cpuinfo(struct seq_file *m, void *v)
{
  String str = _stp_string_init (0);
  _stp_stack_print (0,0);
  _stp_stack_print (1,0);
  _stp_list_add (traces, _stp_stack_sprint(str, 0, 0));

  jprobe_return();
  return 0;
}


static struct jprobe dtr_probes[] = {
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
  {
    .kp.addr = (kprobe_opcode_t *)"show_cpuinfo",
    .entry = (kprobe_opcode_t *) inst_show_cpuinfo,
  },
};

#define MAX_DTR_ROUTINE (sizeof(dtr_probes)/sizeof(struct jprobe))

static unsigned n_subbufs = 4;
module_param(n_subbufs, uint, 0);
MODULE_PARM_DESC(n_subbufs, "number of sub-buffers per per-cpu buffer");

static unsigned subbuf_size = 65536;
module_param(subbuf_size, uint, 0);
MODULE_PARM_DESC(subbuf_size, "size of each per-cpu sub-buffers");

static int pid;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "daemon pid");

static int init_dtr(void)
{
  int ret;
  
  if (!pid) {
	  printk("init_dtr: Can't start without daemon pid\n");		
	  return -1;
  }

  if (_stp_transport_open(n_subbufs, subbuf_size, pid) < 0) {
	  printk("init_dtr: Couldn't open transport\n");		
	  return -1;
  }
  
  opens = _stp_map_new (1000, INT64);
  reads = _stp_map_new (1000, STAT);
  writes = _stp_map_new (1000, STAT);
  traces = _stp_list_new (1000, STRING);

  ret = _stp_register_jprobes (dtr_probes, MAX_DTR_ROUTINE);

  _stp_log("instrumentation is enabled...\n");
  return ret;
}

static int exited; /* FIXME: this is a stopgap - if we don't do this
		    * and are manually removed, bad things happen */

static void probe_exit (void)
{
  struct map_node_stat *st;
  struct map_node_int64 *ptr;
  struct map_node_str *sptr;

  exited = 1;

  _stp_unregister_jprobes (dtr_probes, MAX_DTR_ROUTINE);

  foreach (traces, sptr) {
    _stp_printf ("trace: %s\n", sptr->str);
    _stp_print_flush ();
  }

  foreach (opens, ptr) {
    _stp_printf ("opens[%s] = %lld\n", key1str(ptr), ptr->val); 
    _stp_print_flush ();
  }

  foreach (reads, st) {
    _stp_printf ("reads[%s] = [count=%lld  sum=%lld   min=%lld   max=%lld]\n", key1str(st), 
		st->stats.count, st->stats.sum, st->stats.min, st->stats.max);
    _stp_print_flush ();
  }
  
  foreach (writes, st) {
    _stp_printf ("writes[%s] = [count=%lld  sum=%lld   min=%lld   max=%lld]\n", key1str(st), 
		st->stats.count, st->stats.sum, st->stats.min, st->stats.max);
    _stp_print_flush();
  }

  _stp_map_del (opens);
  _stp_map_del (reads);
  _stp_map_del (writes);
}

static void cleanup_dtr(void)
{
	if (!exited)
		probe_exit();
	
	_stp_transport_close();
}

module_init(init_dtr);
module_exit(cleanup_dtr);
MODULE_LICENSE("GPL");

