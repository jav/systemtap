/* -*- linux-c -*- */

/** Create a new map.
 * Maps must be created at module initialization time.
 * @param max_entries The maximum number of entries allowed. Currently that 
 * will be allocated dynamically.
 * @param type Type of values stored in this map. 
 * @return A MAP on success or NULL on failure.
 */


static unsigned long (*_stp_lookup_name)(char *name)=(void *)KALLSYMS_LOOKUP_NAME;

void _stp_unregister_jprobes (struct jprobe *probes, int num_probes)
{
	int i;
	for (i = 0; i < num_probes; i++)
		unregister_jprobe(&probes[i]);
	dlog ("All jprobes removed\n");
}

int _stp_register_jprobes (struct jprobe *probes, int num_probes)
{
	int i, ret ;
	unsigned long addr;

	for (i = 0; i < num_probes; i++) {
		addr =_stp_lookup_name((char *)probes[i].kp.addr);
		if (addr == 0) {
			dlog ("ERROR: function %s not found!\n", 
			      (char *)probes[i].kp.addr);
			ret = -1; /* FIXME */
			goto out;
		}
		dlog("inserting jprobe at %s (%p)\n", probes[i].kp.addr, addr);
		probes[i].kp.addr = (kprobe_opcode_t *)addr;
		ret = register_jprobe(&probes[i]);
		if (ret)
			goto out;
	}
	return 0;
out:
	dlog ("probe module initialization failed.  Exiting...\n");
	_stp_unregister_jprobes(probes, i);
	return ret;
}

void _stp_unregister_kprobes (struct kprobe *probes, int num_probes)
{
	int i;
	for (i = 0; i < num_probes; i++)
		unregister_kprobe(&probes[i]);
	dlog ("All kprobes removed\n");
}

int _stp_register_kprobes (struct kprobe *probes, int num_probes)
{
	int i, ret ;
	unsigned long addr;

	for (i = 0; i < num_probes; i++) {
		addr =_stp_lookup_name((char *)probes[i].addr);
		if (addr == 0) {
			dlog ("ERROR: function %s not found!\n", 
			      (char *)probes[i].addr);
			ret = -1; /* FIXME */
			goto out;
		}
		dlog("inserting kprobe at %s (%p)\n", probes[i].addr, addr);
		probes[i].addr = (kprobe_opcode_t *)addr;
		ret = register_kprobe(&probes[i]);
		if (ret)
			goto out;
	}
	return 0;
out:
	dlog ("probe module initialization failed.  Exiting...\n");
	_stp_unregister_kprobes(probes, i);
	return ret;
}

