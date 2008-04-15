/* -*- linux-c -*- 
 * symbols.c - stp symbol and module functions
 *
 * Copyright (C) Red Hat Inc, 2006-2008
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * The u32_swap(), generic_swap(), and sort() functions were adapted from
 * lib/sort.c of kernel 2.6.22-rc5. It was written by Matt Mackall.
 */

#ifndef _STP_SYMBOLS_C_
#define _STP_SYMBOLS_C_
#include "../sym.h"

static char *_stp_symbol_data = NULL;
static int _stp_symbol_state = 0;
static char *_stp_module_data = NULL;
static int _stp_module_state = 0;

/* these are all the symbol types we are interested in */
static int _stp_sym_type_ok(int type)
{
	/* we only care about function symbols, which are in the text section */
	if (type == 'T' || type == 't')
		return 1;
	return 0;
}

/* From a module struct, scan the symtab and figure out how much space */
/* we need to store all the parts we are interested in */
static unsigned _stp_get_sym_sizes(struct module *m, unsigned *dsize)
{
	unsigned int i;
	unsigned num = 0, datasize = 0;
	for (i = 0; i < m->num_symtab; i++) {
		char *str = (char *)(m->strtab + m->symtab[i].st_name);
		if (*str != '\0' && _stp_sym_type_ok(m->symtab[i].st_info)) {
			datasize += strlen(str) + 1;
			num++;
		}
	}
	*dsize = datasize;
	return num;
}

/* allocate space for a module, sections, and symbols */
static struct _stp_module *_stp_alloc_module(unsigned sectsize, unsigned num, unsigned datasize)
{
	struct _stp_module *mod = (struct _stp_module *)_stp_kzalloc(sizeof(struct _stp_module));
	if (mod == NULL)
		goto bad;

	mod->sections = (struct _stp_symbol *)_stp_kmalloc(sectsize);
	if (mod->sections == NULL)
		goto bad;

	mod->symbols = (struct _stp_symbol *)_stp_kmalloc(num * sizeof(struct _stp_symbol));
	if (mod->symbols == NULL) {
		mod->symbols = (struct _stp_symbol *)_stp_vmalloc(num * sizeof(struct _stp_symbol));
		if (mod->symbols == NULL)
			goto bad;
		mod->allocated.symbols = 1;
	}

	mod->symbol_data = _stp_kmalloc(datasize);
	if (mod->symbol_data == NULL) {
		mod->symbol_data = _stp_vmalloc(datasize);
		if (mod->symbol_data == NULL)
			goto bad;
		mod->allocated.symbol_data = 1;
	}

	mod->num_symbols = num;
	return mod;

bad:
	if (mod) {
		if (mod->sections)
			_stp_kfree(mod->sections);
		if (mod->symbols) {
			if (mod->allocated.symbols)
				_stp_vfree(mod->symbols);
			else
				_stp_kfree(mod->symbols);
		}
		_stp_kfree(mod);
	}
	return NULL;
}

static void _stp_free_module(struct _stp_module *mod)
{
	/* The module write lock is held. Any prior readers of this */
	/* module's data will have read locks and need to finish before */
	/* the memory is freed. */
	write_lock(&mod->lock);
	write_unlock(&mod->lock);	/* there will be no more readers */

	/* Free symbol memory */
	/* If symbol_data wasn't allocated, then symbols weren't either. */
	if (mod->symbol_data) {
		if (mod->symbols) {
			if (mod->allocated.symbols)
				_stp_vfree(mod->symbols);
			else
				_stp_kfree(mod->symbols);
		}
		if (mod->allocated.symbol_data)
			_stp_vfree(mod->symbol_data);
		else
			_stp_kfree(mod->symbol_data);
	}
	if (mod->unwind_data) {
		if (mod->allocated.unwind_data)
			_stp_vfree(mod->unwind_data);
		else
			_stp_kfree(mod->unwind_data);
	}
	if (mod->unwind_hdr) {
		if (mod->allocated.unwind_hdr)
			_stp_vfree(mod->unwind_hdr);
		else
			_stp_kfree(mod->unwind_hdr);
	}
	if (mod->sections)
		_stp_kfree(mod->sections);

	/* free module memory */
	_stp_kfree(mod);
}

/* Delete a module and free its memory. */
/* The module lock should already be held before calling this. */
static void _stp_del_module(struct _stp_module *mod)
{
	int i, num;

	dbug_sym(1, "deleting module %s\n", mod->name);

	/* signal relocation code to clear its cache */
	_stp_module_relocate((char *)-1, NULL, 0);

	/* remove module from the arrays */
	for (num = 0; num < _stp_num_modules; num++) {
		if (_stp_modules[num] == mod)
			break;
	}
	if (num >= _stp_num_modules)
		return;

	for (i = num; i < _stp_num_modules - 1; i++)
		_stp_modules[i] = _stp_modules[i + 1];

	for (num = 0; num < _stp_num_modules; num++) {
		if (_stp_modules_by_addr[num] == mod)
			break;
	}
	for (i = num; i < _stp_num_modules - 1; i++)
		_stp_modules_by_addr[i] = _stp_modules_by_addr[i + 1];

	_stp_num_modules--;

	_stp_free_module(mod);
}

static void _stp_free_modules(void)
{
	int i;
	/* This only happens when the systemtap module unloads */
	/* so there is no need for locks. */
	for (i = _stp_num_modules - 1; i >= 0; i--)
		_stp_del_module(_stp_modules[i]);
}

static unsigned long _stp_kallsyms_lookup_name(const char *name);
static void _stp_create_unwind_hdr(struct _stp_module *m);

extern unsigned _stp_num_kernel_symbols;
extern struct _stp_symbol _stp_kernel_symbols[];

/* initialize the kernel symbols */
static int _stp_init_kernel_symbols(void)
{
	_stp_modules[0] = (struct _stp_module *)_stp_kzalloc(sizeof(struct _stp_module));
	if (_stp_modules[0] == NULL) {
		_dbug("cannot allocate memory\n");
		return -1;
	}
	_stp_modules[0]->symbols = _stp_kernel_symbols;
	_stp_modules[0]->num_symbols = _stp_num_kernel_symbols;
	rwlock_init(&_stp_modules[0]->lock);
	_stp_num_modules = 1;

	/* Note: this mapping is used by kernel/_stext pseudo-relocations. */
	_stp_modules[0]->text = _stp_kallsyms_lookup_name("_stext");
	if (_stp_modules[0]->text == 0) {
	  _dbug("Lookup of _stext failed. Exiting.\n");
	  return -1;
	}
	_stp_modules[0]->data = _stp_kallsyms_lookup_name("_etext");
	if (_stp_modules[0]->data == 0) {
	  _dbug("Lookup of _etext failed. Exiting.\n");
	  return -1;
	}
	_stp_modules[0]->text_size = _stp_modules[0]->data - _stp_modules[0]->text;
	_stp_modules_by_addr[0] = _stp_modules[0];
	
	_stp_kretprobe_trampoline = _stp_kallsyms_lookup_name("kretprobe_trampoline");
	/* Lookup failure is not fatal */

	return 0;
}

static void _stp_do_unwind_data(const char __user *buf, size_t count)
{
	u32 unwind_len;
	unsigned long flags;
	char name[STP_MODULE_NAME_LEN];
	int i;
	struct _stp_module *m;

	dbug_unwind(1, "got unwind data, count=%d\n", count);

	if (count < STP_MODULE_NAME_LEN + sizeof(unwind_len)) {
		dbug_unwind(1, "unwind message too short\n");
		return;
	}
	if (strncpy_from_user(name, buf, STP_MODULE_NAME_LEN) < 0) {
		errk("userspace copy failed\n");
		return;
	}
	dbug_unwind(1, "name=%s\n", name);
	if (!strcmp(name,"*")) {
		/* OK, all initial unwind data received. Ready to go. */
		_stp_ctl_send(STP_TRANSPORT, NULL, 0);
		return;
	}
	count -= STP_MODULE_NAME_LEN;
	buf += STP_MODULE_NAME_LEN;

	if (get_user(unwind_len, (u32 __user *)buf)) {
		errk("userspace copy failed\n");
		return;
	}
	count -= sizeof(unwind_len);
	buf += sizeof(unwind_len);
	if (count != unwind_len) {
		dbug_unwind(1, "count=%d unwind_len=%d\n", (int)count, (int)unwind_len);
		return;
	}

	STP_RLOCK_MODULES;
	for (i = 0; i < _stp_num_modules; i++) {
		if (strcmp(name, _stp_modules[i]->name) == 0)
			break;
	}
	if (unlikely(i == _stp_num_modules)) {
		dbug_unwind(1, "module %s not found!\n", name);
		STP_RUNLOCK_MODULES;
		return;
	}
	m = _stp_modules[i];
	write_lock(&m->lock);
	STP_RUNLOCK_MODULES;

	/* allocate space for unwind data */
	m->unwind_data = _stp_kmalloc(count);
	if (unlikely(m->unwind_data == NULL)) {
		m->unwind_data = _stp_vmalloc(count);
		if (m->unwind_data == NULL) {
			errk("kmalloc failed\n");
			goto done;
		}
		m->allocated.unwind_data = 1;
	}

	if (unlikely(copy_from_user(m->unwind_data, buf, count))) {
		errk("userspace copy failed\n");
		if (m->unwind_data) {
			if (m->allocated.unwind_data)
				_stp_vfree(m->unwind_data);
			else
				_stp_kfree(m->unwind_data);
			m->unwind_data = NULL;
		}
		goto done;
	}
	m->unwind_data_len = count;
	_stp_create_unwind_hdr(m);
done:
	write_unlock(&m->lock);
}

static int _stp_compare_addr(const void *p1, const void *p2)
{
	struct _stp_symbol *s1 = (struct _stp_symbol *)p1;
	struct _stp_symbol *s2 = (struct _stp_symbol *)p2;
	if (s1->addr == s2->addr)
		return 0;
	if (s1->addr < s2->addr)
		return -1;
	return 1;
}

static void _stp_swap_symbol(void *x, void *y, int size)
{
	struct _stp_symbol *a = (struct _stp_symbol *)x;
	struct _stp_symbol *b = (struct _stp_symbol *)y;
	unsigned long addr = a->addr;
	const char *symbol = a->symbol;
	a->addr = b->addr;
	a->symbol = b->symbol;
	b->addr = addr;
	b->symbol = symbol;
}

static void u32_swap(void *a, void *b, int size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

static void generic_swap(void *a, void *b, int size)
{
	do {
		char t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

/**
 * sort - sort an array of elements
 * @base: pointer to data to sort
 * @num: number of elements
 * @size: size of each element
 * @cmp: pointer to comparison function
 * @swap: pointer to swap function or NULL
 *
 * This function does a heapsort on the given array. You may provide a
 * swap function optimized to your element type.
 *
 * Sorting time is O(n log n) both on average and worst-case. While
 * qsort is about 20% faster on average, it suffers from exploitable
 * O(n*n) worst-case behavior and extra memory requirements that make
 * it less suitable for kernel use.
*/
void _stp_sort(void *base, size_t num, size_t size,
	       int (*cmp) (const void *, const void *), void (*swap) (void *, void *, int size))
{
	/* pre-scale counters for performance */
	int i = (num / 2 - 1) * size, n = num * size, c, r;

	if (!swap)
		swap = (size == 4 ? u32_swap : generic_swap);

	/* heapify */
	for (; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r = c) {
			c = r * 2 + size;
			if (c < n - size && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			swap(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i >= 0; i -= size) {
		swap(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			swap(base + r, base + c, size);
		}
	}
}

/* filter out section names we don't care about */
static int _stp_section_is_interesting(const char *name)
{
	int ret = 1;
	if (!strncmp("__", name, 2)
	    || !strncmp(".note", name, 5)
	    || !strncmp(".gnu", name, 4)
	    || !strncmp(".mod", name, 4))
		ret = 0;
	return ret;
}

/* Create a new _stp_module and load the symbols */
static struct _stp_module *_stp_load_module_symbols(struct module *mod)
{
	int i, num, overflow = 0;
	struct module_sect_attrs *sa = mod->sect_attrs;
        struct attribute_group *sag = & sa->grp;
	unsigned sect_size = 0, sect_num = 0, sym_size, sym_num;
	struct _stp_module *sm;
	char *dataptr, *endptr;
        unsigned nsections = 0;

#ifdef STAPCONF_MODULE_NSECTIONS
        nsections = sa->nsections;
#else
        /* count section attributes on older kernel */
        struct attribute** gattr;
        for (gattr = sag->attrs; *gattr; gattr++)
          nsections++;
        dbug_sym(2, "\tcount %d\n", nsections);
#endif

	/* calculate how much space to allocate for section strings */
	for (i = 0; i < nsections; i++) {
		if (_stp_section_is_interesting(sa->attrs[i].name)) {
			sect_num++;
			sect_size += strlen(sa->attrs[i].name) + 1;
			dbug_sym(2, "\t%s\t%lx\n", sa->attrs[i].name, sa->attrs[i].address);
		}
	}
	sect_size += sect_num * sizeof(struct _stp_symbol);

	/* and how much space for symbols */
	sym_num = _stp_get_sym_sizes(mod, &sym_size);

	sm = _stp_alloc_module(sect_size, sym_num, sym_size);
	if (!sm) {
		errk("failed to allocate memory for module.\n");
		return NULL;
	}

	strlcpy(sm->name, mod->name, STP_MODULE_NAME_LEN);
	sm->module = (unsigned long)mod;
	sm->text = (unsigned long)mod->module_core;
	sm->text_size = mod->core_text_size;
	sm->data = 0;		/* fixme */
	sm->num_sections = sect_num;
	rwlock_init(&sm->lock);

	/* copy in section data */
	dataptr = (char *)((long)sm->sections + sect_num * sizeof(struct _stp_symbol));
	endptr = (char *)((long)sm->sections + sect_size);
	num = 0;
	for (i = 0; i < nsections; i++) {
		size_t len, maxlen;
		if (_stp_section_is_interesting(sa->attrs[i].name)) {
			sm->sections[num].addr = sa->attrs[i].address;
			sm->sections[num].symbol = dataptr;
			maxlen = (size_t) (endptr - dataptr);
			len = strlcpy(dataptr, sa->attrs[i].name, maxlen);
			if (unlikely(len >= maxlen)) {
				_dbug("dataptr=%lx endptr=%lx len=%d maxlen=%d\n", dataptr, endptr, len, maxlen);
				overflow = 1;
			}
			dataptr += len + 1;
			num++;
		}
	}
	if (unlikely(overflow)) {
		errk("Section names truncated!!! Should never happen!!\n");
		*endptr = 0;
		overflow = 0;
	}

	/* now copy all the symbols we are interested in */
	dataptr = sm->symbol_data;
	endptr = dataptr + sym_size - 1;
	num = 0;
	for (i = 0; i < mod->num_symtab; i++) {
		char *str = (char *)(mod->strtab + mod->symtab[i].st_name);
		if (*str != '\0' && _stp_sym_type_ok(mod->symtab[i].st_info)) {
			sm->symbols[num].symbol = dataptr;
			sm->symbols[num].addr = mod->symtab[i].st_value;
			while (*str && (dataptr < endptr))
				*dataptr++ = *str++;
			if (unlikely(*str))
				overflow = 1;
			*dataptr++ = 0;
			num++;
		}
	}
	if (unlikely(overflow))
		errk("Symbol names truncated!!! Should never happen!!\n");

	/* sort symbols by address */
	_stp_sort(sm->symbols, num, sizeof(struct _stp_symbol), _stp_compare_addr, _stp_swap_symbol);

	return sm;
}

/* Remove any old module info from our database. */
static void _stp_module_exists_delete(struct _stp_module *mod)
{
	int i, num;
	/* remove any old modules with the same name */
	for (num = 1; num < _stp_num_modules; num++) {
		if (strcmp(_stp_modules[num]->name, mod->name) == 0) {
			dbug_sym(1, "found existing module with name %s. Deleting.\n", mod->name);
			_stp_del_module(_stp_modules[num]);
			break;
		}
	}

	/* remove modules with overlapping addresses */
	for (num = 1; num < _stp_num_modules; num++) {
		if (mod->text + mod->text_size < _stp_modules_by_addr[num]->text)
			continue;
		if (mod->text < _stp_modules_by_addr[num]->text + _stp_modules_by_addr[num]->text_size) {
			dbug_sym(1, "New module %s overlaps with old module %s. Deleting old.\n",
				 mod->name, _stp_modules_by_addr[num]->name);
			_stp_del_module(_stp_modules_by_addr[num]);
		}
	}

}

static void _stp_ins_module(struct module *mod)
{
	int i, num, res;
	unsigned long flags;
	struct _stp_module *m;
	dbug_sym(1, "insert %s\n", mod->name);
	m = _stp_load_module_symbols(mod);
	if (m == NULL)
		return;

	STP_WLOCK_MODULES;
	_stp_module_exists_delete(m);
	/* check for overflow */
	if (_stp_num_modules == STP_MAX_MODULES) {
		errk("Exceeded the limit of %d modules\n", STP_MAX_MODULES);
		goto done;
	}

	/* insert alphabetically in _stp_modules[] */
	for (num = 1; num < _stp_num_modules; num++)
		if (strcmp(_stp_modules[num]->name, m->name) > 0)
			break;
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules[i] = _stp_modules[i - 1];
	_stp_modules[num] = m;
	/* insert by text address in _stp_modules_by_addr[] */
	for (num = 1; num < _stp_num_modules; num++)
		if (m->text < _stp_modules_by_addr[num]->text)
			break;
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules_by_addr[i] = _stp_modules_by_addr[i - 1];
	_stp_modules_by_addr[num] = m;
	_stp_num_modules++;
done:
	STP_WUNLOCK_MODULES;
	return;
}

static int _stp_module_load_notify(struct notifier_block *self, unsigned long val, void *data)
{
	struct module *mod = (struct module *)data;
	struct _stp_module rmod;
	switch (val) {
	case MODULE_STATE_COMING:
		dbug_sym(1, "module %s load notify\n", mod->name);
		_stp_ins_module(mod);
		break;
	default:
		errk("module loaded? val=%ld\n", val);
	}
	return 0;
}

static struct notifier_block _stp_module_load_nb = {
	.notifier_call = _stp_module_load_notify,
};

#include <linux/seq_file.h>

static int _stp_init_modules(void)
{
	loff_t pos = 0;
	void *res;
	struct module *mod;
	const struct seq_operations *modules_op = (const struct seq_operations *)_stp_kallsyms_lookup_name("modules_op");
	
	if (modules_op == NULL) {
	  _dbug("Lookup of modules_op failed.\n");
	  return -1;
	}

	/* Use the seq_file interface to safely get a list of installed modules */
	res = modules_op->start(NULL, &pos);
	while (res) {
		mod = list_entry(res, struct module, list);
		_stp_ins_module(mod);
		res = modules_op->next(NULL, res, &pos);
	}

	if (register_module_notifier(&_stp_module_load_nb))
		errk("failed to load module notifier\n");

	/* unlocks the list */
	modules_op->stop(NULL, NULL);

#ifdef STP_USE_DWARF_UNWINDER
	/* now that we have all the modules, ask for their unwind info */
	{
		unsigned long flags;
		int i, left = STP_CTL_BUFFER_SIZE;
		char buf[STP_CTL_BUFFER_SIZE];
		char *ptr = buf;
		*ptr = 0;

		STP_RLOCK_MODULES;
		/* Loop through modules, sending module names packed into */
		/* messages of size STP_CTL_BUFFER. */
		for (i = 0; i < _stp_num_modules; i++) {
			char *name = _stp_modules[i]->name;
			int len = strlen(name);
			if (len >= left) {
				_stp_ctl_send(STP_UNWIND, buf, sizeof(buf) - left);
				ptr = buf;
				left = STP_CTL_BUFFER_SIZE;
			}
			strlcpy(ptr, name, left);
			ptr += len + 1;
			left -= len + 1;
		}
		STP_RUNLOCK_MODULES;

		/* Send terminator.  When we get this back from stapio */
		/* that means all the unwind info has been sent. */
		strlcpy(ptr, "*", left);
		left -= 2;
		_stp_ctl_send(STP_UNWIND, buf, sizeof(buf) - left);
	}
#else
	/* done with modules, now go */
	_stp_ctl_send(STP_TRANSPORT, NULL, 0);
#endif /* STP_USE_DWARF_UNWINDER */

	return 0;
}

#endif /* _STP_SYMBOLS_C_ */
