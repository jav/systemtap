#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */

#include "runtime.h"
#include "io.c"
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
};

#define MAX_DTR_ROUTINE (sizeof(dtr_probes)/sizeof(struct jprobe))

static int init_dtr(void)
{
  int ret;
  
  opens = _stp_map_new (1000, INT64);
  reads = _stp_map_new (1000, STAT);
  writes = _stp_map_new (1000, STAT);

  ret = _stp_register_jprobes (dtr_probes, MAX_DTR_ROUTINE);

  dlog("instrumentation is enabled...\n");
  return ret;

}

static void cleanup_dtr(void)
{
  struct map_node_stat *st;
  struct map_node_int64 *ptr;

  _stp_unregister_jprobes (dtr_probes, MAX_DTR_ROUTINE);

  for (ptr = (struct map_node_int64 *)_stp_map_start(opens); ptr; 
       ptr = (struct map_node_int64 *)_stp_map_iter (opens,(struct map_node *)ptr))
    dlog ("opens[%s] = %lld\n", key1str(ptr), ptr->val); 
  dlog ("\n");

  for (st = (struct map_node_stat *)_stp_map_start(reads); st; 
       st = (struct map_node_stat *)_stp_map_iter (reads,(struct map_node *)st))
    dlog ("reads[%s] = [count=%lld  sum=%lld   min=%lld   max=%lld]\n", key1str(st), st->stats.count, st->stats.sum,
	    st->stats.min, st->stats.max);
  dlog ("\n");

  for (st = (struct map_node_stat *)_stp_map_start(writes); st; 
       st = (struct map_node_stat *)_stp_map_iter (writes,(struct map_node *)st))
    dlog ("writes[%s] = [count=%lld  sum=%lld   min=%lld   max=%lld]\n", key1str(st), st->stats.count, st->stats.sum,
	    st->stats.min, st->stats.max);
  dlog ("\n");

  _stp_map_del (opens);
  _stp_map_del (reads);
  _stp_map_del (writes);

  dlog("EXIT\n");
}

module_init(init_dtr);
module_exit(cleanup_dtr);
MODULE_LICENSE("GPL");

