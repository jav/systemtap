/* Framework for putting a jprobe in a tasklet. */
/* Useful for testing probes in interrupt context. */
/* Doesn't do anything useful as is.  Put test code in the inst func */

#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */

#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("test jprobes of tasklets");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");

void inst__rcu_process_callbacks(struct rcu_ctrlblk *rcp,
				 struct rcu_state *rsp, struct rcu_data *rdp)
{
  _stp_log ("interrupt=%d\n", in_interrupt());
  jprobe_return();
}

static struct jprobe stp_probes[] = {
  {
    .kp.addr =  (kprobe_opcode_t *)"__rcu_process_callbacks",
    .entry = (kprobe_opcode_t *) inst__rcu_process_callbacks
  },
};
#define MAX_STP_PROBES (sizeof(stp_probes)/sizeof(struct jprobe))


static int init_stp(void)
{
  int ret;

  if (_stp_netlink_open() < 0)
    return -1;
  ret = _stp_register_jprobes (stp_probes, MAX_STP_PROBES);
  _stp_log ("instrumentation is enabled...\n");
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (stp_probes, MAX_STP_PROBES);
  _stp_log ("EXIT\n");

}
static void cleanup_stp(void)
{
  _stp_netlink_close();
}

module_init(init_stp);
module_exit(cleanup_stp);
MODULE_LICENSE("GPL");

