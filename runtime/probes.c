#ifndef _PROBES_C_ /* -*- linux-c -*- */
#define _PROBES_C

/** @file probes.c
 * @brief Functions to assist loading and unloading groups of probes.
 */

/** Lookup name.
 * This simply calls the kernel function kallsyms_lookup_name().
 * That function is not exported, so this workaround is required.
 * See the kernel source, kernel/kallsyms.c for more information.
 */
static unsigned long (*_stp_lookup_name)(char *name)=(void *)KALLSYMS_LOOKUP_NAME;

/** Unregister a group of jprobes.
 * @param probes Pointer to an array of struct jprobe.
 * @param num_probes Number of probes in the array.
 */

void _stp_unregister_jprobes (struct jprobe *probes, int num_probes)
{
	int i;
	for (i = 0; i < num_probes; i++)
		unregister_jprobe(&probes[i]);
	dbug("All jprobes removed\n");
}

/** Register a group of jprobes.
 * @param probes Pointer to an array of struct jprobe.
 * @param num_probes Number of probes in the array.
 * @return 0 on success.
 */

int _stp_register_jprobes (struct jprobe *probes, int num_probes)
{
	int i, ret ;
	unsigned long addr;

	for (i = 0; i < num_probes; i++) {
		addr =_stp_lookup_name((char *)probes[i].kp.addr);
		if (addr == 0) {
			_stp_warn("function %s not found!\n", (char *)probes[i].kp.addr);
			ret = -1; /* FIXME */
			goto out;
		}
		dbug("inserting jprobe at %s (%p)\n", probes[i].kp.addr, addr);
		probes[i].kp.addr = (kprobe_opcode_t *)addr;
		ret = register_jprobe(&probes[i]);
		if (ret)
			goto out;
	}
	return 0;
out:
	_stp_warn("probe module initialization failed.  Exiting...\n");
	_stp_unregister_jprobes(probes, i);
	return ret;
}

/** Unregister a group of kprobes.
 * @param probes Pointer to an array of struct kprobe.
 * @param num_probes Number of probes in the array.
 */

void _stp_unregister_kprobes (struct kprobe *probes, int num_probes)
{
	int i;
	for (i = 0; i < num_probes; i++)
		unregister_kprobe(&probes[i]);
	dbug("All kprobes removed\n");
}


#ifdef USE_RET_PROBES
/** Unregister a group of return probes.
 * @param probes Pointer to an array of struct kretprobe.
 * @param num_probes Number of probes in the array.
 */
void _stp_unregister_kretprobes (struct kretprobe *probes, int num_probes)
{
	int i;
	for (i = 0; i < num_probes; i++)
		unregister_kretprobe(&probes[i]);
	dbug("All return probes removed\n");
}
#endif

/** Register a group of kprobes.
 * @param probes Pointer to an array of struct kprobe.
 * @param num_probes Number of probes in the array.
 * @return 0 on success.
 */
int _stp_register_kprobes (struct kprobe *probes, int num_probes)
{
	int i, ret ;
	unsigned long addr;

	for (i = 0; i < num_probes; i++) {
		addr =_stp_lookup_name((char *)probes[i].addr);
		if (addr == 0) {
			_stp_warn("function %s not found!\n", (char *)probes[i].addr);
			ret = -1;
			goto out;
		}
		dbug("inserting kprobe at %s (%p)\n", probes[i].addr, addr);
		probes[i].addr = (kprobe_opcode_t *)addr;
		ret = register_kprobe(&probes[i]);
		if (ret)
			goto out;
	}
	return 0;
out:
	_stp_warn("probe module initialization failed.  Exiting...\n");
	_stp_unregister_kprobes(probes, i);
	return ret;
}

#ifdef USE_RET_PROBES
/** Register a group of return probes.
 * @param probes Pointer to an array of struct kretprobe.
 * @param num_probes Number of probes in the array.
 * @return 0 on success.
 */
int _stp_register_kretprobes (struct kretprobe *probes, int num_probes)
{
	int i, ret ;
	unsigned long addr;

	for (i = 0; i < num_probes; i++) {
		addr =_stp_lookup_name((char *)probes[i].kp.addr);
		if (addr == 0) {
			_stp_warn("function %s not found!\n", 
				   (char *)probes[i].kp.addr);
			ret = -1; /* FIXME */
			goto out;
		}
		dbug("inserting kretprobe at %s (%p)\n", probes[i].kp.addr, addr);
		probes[i].kp.addr = (kprobe_opcode_t *)addr;
		ret = register_kretprobe(&probes[i]);
		if (ret)
			goto out;
	}
	return 0;
out:
	_stp_warn("probe module initialization failed.  Exiting...\n");
	_stp_unregister_kretprobes(probes, i);
	return ret;
}
#endif
#endif /* _PROBES_C */
