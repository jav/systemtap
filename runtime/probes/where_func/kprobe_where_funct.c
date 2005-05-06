/* kprobe_where_funct.c
   this is a simple module to get information about calls to a function that is passed as a module option
   Will Cohen
*/

#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16		/* largest histogram width */
#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "map.c"
#include "probes.c"
#include "current.c"
#include "sym.c"

MODULE_DESCRIPTION("SystemTap probe: where_func");
MODULE_AUTHOR("Will Cohen and Martin Hunt");

static char default_name[] = "schedule";
static char *funct_name = default_name;
module_param(funct_name, charp, 0);
MODULE_PARM_DESC(funct_name, "function entry name.\n");

static int count_funct = 0;

MAP funct_locations;

static int inst_funct(struct kprobe *p, struct pt_regs *regs)
{
	long ret_addr = _stp_ret_addr(regs);
	++count_funct;
	_stp_map_key_long(funct_locations, ret_addr);
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

static unsigned n_subbufs = 4;
module_param(n_subbufs, uint, 0);
MODULE_PARM_DESC(n_subbufs, "number of sub-buffers per per-cpu buffer");

static unsigned subbuf_size = 65536;
module_param(subbuf_size, uint, 0);
MODULE_PARM_DESC(subbuf_size, "size of each per-cpu sub-buffers");

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

	if (_stp_transport_open(n_subbufs, subbuf_size, pid) < 0) {
		printk("init_dtr: Couldn't open transport\n");		
		return -1;
	}
  
	funct_locations = _stp_map_new(1000, INT64);

	if (funct_name)
		kp[0].addr = funct_name;

	ret = _stp_register_kprobes (kp, MAX_KPROBES);

	return ret;
}

static int exited; /* FIXME: this is a stopgap - if we don't do this
		    * and are manually removed, bad things happen */

static void probe_exit (void)
{
	struct map_node_int64 *ptr;

	exited = 1;
	
	_stp_unregister_kprobes (kp, MAX_KPROBES);

	_stp_printf("%s() called %d times.\n", funct_name, count_funct);
	_stp_printf("NUM\tCaller\n", funct_name);

	/* now walk the hash table and print out all the information */
	foreach(funct_locations, ptr) {
		_stp_printf("%lld\t", ptr->val);
		_stp_symbol_print (key1int(ptr));
		_stp_print_flush();
	}
	
	_stp_map_del(funct_locations);
}

void cleanup_module(void)
{
	if (!exited)
		probe_exit();
	
	_stp_transport_close();
}

MODULE_LICENSE("GPL");
