#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

struct kp_data {
	struct kprobe kp;
	atomic_t use_count;
};

static int handler_pre(struct kprobe *p, struct pt_regs *regs);

#include "kprobe_defs.h"


/* kprobe pre_handler: called just before the probed instruction is executed */
static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct kp_data *k = container_of(p, struct kp_data, kp);
	atomic_inc(&k->use_count);
	return 0;
}

static int __init kprobe_init(void)
{
	int ret;
	int probes_registered = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(kp_data); i++) {
		ret = register_kprobe(&kp_data[i].kp);
		if (ret != 0)
			atomic_set(&kp_data[i].use_count, -1);
		else
			probes_registered++;
	}
	if (probes_registered == 0) {
		for (i = 0; i < ARRAY_SIZE(kp_data); i++) {
			printk(KERN_INFO "-1 %s\n", kp_data[i].kp.symbol_name);
		}
		printk(KERN_INFO "kprobe_module unloaded\n");
		return ret;
	}
	printk(KERN_INFO "Planted kprobes\n");
	return 0;
}

static void __exit kprobe_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kp_data); i++) {
		if (atomic_read(&kp_data[i].use_count) != -1)
			unregister_kprobe(&kp_data[i].kp);
	}
	printk(KERN_INFO "kprobes unregistered\n");
	for (i = 0; i < ARRAY_SIZE(kp_data); i++) {
		printk(KERN_INFO "%d %s\n", atomic_read(&kp_data[i].use_count),
		       kp_data[i].kp.symbol_name);
	}
	printk(KERN_INFO "kprobe_module unloaded\n");
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
