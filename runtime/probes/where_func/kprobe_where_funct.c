/* kprobe_where_funct.c
   this is a simple module to get information about calls to a function 
   that is passed as a module option
   Will Cohen
*/

#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

static unsigned n_subbufs = 4;
static unsigned subbuf_size = 65536;

#include "runtime.h"

#ifdef STP_NETLINK_ONLY
static int transport_mode = STP_TRANSPORT_NETLINK;
#else
static int transport_mode = STP_TRANSPORT_RELAYFS;
#endif

#define NEED_INT64_VALS

#define KEY1_TYPE INT64
#include "map-keys.c"

#include "map.c"
#include "probes.c"
#include "sym.c"
#include "current.c"

MODULE_DESCRIPTION("SystemTap probe: where_func");
MODULE_AUTHOR("Will Cohen and Martin Hunt");

static char default_name[] = "schedule";
static char *funct_name = default_name;
module_param(funct_name, charp, 0);
MODULE_PARM_DESC(funct_name, "function entry name.\n");

MAP funct_locations;

static int inst_funct(struct kprobe *p, struct pt_regs *regs)
{
  long ret_addr = _stp_ret_addr(regs);
  _stp_map_key_int64(funct_locations, ret_addr);
  _stp_map_add_int64(funct_locations, 1);
  return 0;
}

/*For each probe you need to allocate a kprobe structure*/
static struct kprobe kp[] = {
	{
		.addr = default_name,
		.pre_handler = inst_funct,
	}
};
#define MAX_KPROBES (sizeof(kp)/sizeof(struct kprobe))


static int pid;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "daemon pid");

int init_module(void)
{
	int ret;

	if (!pid) {
		printk("init_dtr: Can't start without daemon pid\n");		
		return -1;
	}

	if (_stp_transport_open(transport_mode, n_subbufs, subbuf_size, pid) < 0) {
		printk("init_dtr: Couldn't open transport\n");		
		return -1;
	}
  
	funct_locations = _stp_map_new_int64 (1000, INT64);

	if (funct_name)
		kp[0].addr = funct_name;

	ret = _stp_register_kprobes (kp, MAX_KPROBES);

	return ret;
}

static void probe_exit (void)
{
	_stp_unregister_kprobes (kp, MAX_KPROBES);

	_stp_map_print (funct_locations, "Count: %d\tCaller: %1P");
	_stp_map_del(funct_locations);
}

void cleanup_module(void)
{
	_stp_transport_close();
}

MODULE_LICENSE("GPL");
