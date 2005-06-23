/* Framework for putting a jprobe in a tasklet. */
/* Useful for testing probes in interrupt context. */
/* Doesn't do anything useful as is.  Put test code in the inst func */

#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "probes.c"

MODULE_DESCRIPTION("test jprobes of tasklets");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");

void inst__rcu_process_callbacks(struct rcu_ctrlblk *rcp,
				 struct rcu_state *rsp, struct rcu_data *rdp)
{  
  _stp_printf ("count=%d irqs_disabled=%d in_interrupt=%d in_irq=%d", 
	       preempt_count(), irqs_disabled(), in_interrupt(), in_irq());
  _stp_print_flush();
  jprobe_return();
}

static struct jprobe stp_probes[] = {
  {
    .kp.addr =  (kprobe_opcode_t *)"__rcu_process_callbacks",
    .entry = (kprobe_opcode_t *) inst__rcu_process_callbacks
  },
};
#define MAX_STP_PROBES (sizeof(stp_probes)/sizeof(struct jprobe))

int init_module(void)
{
  int ret;
  
  TRANSPORT_OPEN;
  ret = _stp_register_jprobes (stp_probes, MAX_STP_PROBES);
  return ret;
}

static void probe_exit (void)
{
  _stp_unregister_jprobes (stp_probes, MAX_STP_PROBES);
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

