/* kprobe_where_funct.c
   this is a simple module to get information about calls to a function that is passed as a module option
   Will Cohen
*/

#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16		/* largest histogram width */

#include "runtime.h"
#include "io.c"
#include "map.c"
#include "probes.c"

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
	long ret_addr = cur_ret_addr(regs);
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

int init_module(void)
{
	int ret;

	funct_locations = _stp_map_new(1000, INT64);

	if (funct_name)
		kp[0].addr = funct_name;

	ret = _stp_register_kprobes (kp, MAX_KPROBES);

	return ret;
}

void cleanup_module(void)
{
	struct map_node_int64 *ptr;

	_stp_unregister_kprobes (kp, MAX_KPROBES);

	dlog("%s() called %d times.\n", funct_name, count_funct);
	dlog("NUM\tCaller Addr\tCaller Name\n", funct_name);

	/* now walk the hash table and print out all the information */
	foreach(funct_locations, ptr) {
		_stp_print_buf_init();
		_stp_print_symbol("%s\n", key1int(ptr));
		dlog("%lld\t0x%p\t(%s)\n", ptr->val, key1int(ptr), _stp_pbuf);
	}

	_stp_map_del(funct_locations);
}

MODULE_LICENSE("GPL");
