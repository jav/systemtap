#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */
#include "../../runtime.h"

#include "../../io.c"
#include "../../map.c"


MODULE_PARM_DESC(dtr, "\n");

MAP opens, reads, writes;

asmlinkage long inst_sys_open (const char __user * filename, int flags, int mode)
{
  _stp_map_key_str (opens, current->comm);
  _stp_map_set_int64 (opens, _stp_map_get_int64(opens) + 1);
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
    .kp.addr = (kprobe_opcode_t *)0xc0166f32,
    .entry = (kprobe_opcode_t *) inst_sys_open
  },
  {
    .kp.addr = (kprobe_opcode_t *)0xc0167b93,
    .entry = (kprobe_opcode_t *) inst_sys_read
  },
  {
    .kp.addr = (kprobe_opcode_t *)0xc0167bf5,
    .entry = (kprobe_opcode_t *) inst_sys_write
  },
};

#define MAX_DTR_ROUTINE (sizeof(dtr_probes)/sizeof(struct jprobe))

static int init_dtr(void)
{
  int i;
  
  opens = _stp_map_new (1000, INT64);
  reads = _stp_map_new (1000, STAT);
  writes = _stp_map_new (1000, STAT);

  for (i = 0; i < MAX_DTR_ROUTINE; i++) {
    printk("DTR: plant jprobe at %p, handler addr %p\n",
	   dtr_probes[i].kp.addr, dtr_probes[i].entry);
    register_jprobe(&dtr_probes[i]);
  }
  printk("DTR: instrumentation is enabled...\n");
  return 0;
}

static void cleanup_dtr(void)
{
  int i;
  struct map_node_stat *st;
  struct map_node_int64 *ptr;

  for (i = 0; i < MAX_DTR_ROUTINE; i++)
    unregister_jprobe(&dtr_probes[i]);

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

  printk("DTR: EXIT\n");
}

module_init(init_dtr);
module_exit(cleanup_dtr);
MODULE_LICENSE("GPL");

