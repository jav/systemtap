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

#ifndef _SYMBOLS_C_
#define _SYMBOLS_C_
#include "../sym.h"

static char *_stp_symbol_data = NULL;
static int _stp_symbol_state = 0;
static char *_stp_module_data = NULL;
static int _stp_module_state = 0;


/* these are all the symbol types we are interested in */
static int _stp_sym_type_ok(int type)
{
	switch (type) {
	case 'T':
	case 't':
		return 1;
	default:
		return 0;
	}
	return 0;
}

/* From a module struct, scan the symtab and figure out how much space */
/* we need to store all the parts we are interested in */
static unsigned _stp_get_sym_sizes(struct module *m, unsigned *dsize)
{
	unsigned int i;
	unsigned num = 0, datasize = 0;
	for (i=0; i < m->num_symtab; i++) {
		char *str = (char *)(m->strtab + m->symtab[i].st_name);
		if (*str != '\0' && _stp_sym_type_ok(m->symtab[i].st_info)) {
			datasize += strlen(str)+1;
			num++;
		}
	}
	*dsize = datasize;
	return num;
}

/* allocate space for a module and symbols */
static struct _stp_module * _stp_alloc_module(unsigned num, unsigned datasize, unsigned unwindsize)
{
	struct _stp_module *mod = (struct _stp_module *)_stp_kzalloc(sizeof(struct _stp_module));
	if (mod == NULL)
		goto bad;

	mod->symbols = (struct _stp_symbol *)_stp_kmalloc(num * sizeof(struct _stp_symbol));
	if (mod->symbols == NULL) {
		mod->symbols = (struct _stp_symbol *)_stp_vmalloc(num * sizeof(struct _stp_symbol));
		if (mod->symbols == NULL)
			goto bad;
		mod->allocated = 1;
	}

	mod->symbol_data = _stp_kmalloc(datasize);
	if (mod->symbol_data == NULL) {
		mod->symbol_data = _stp_vmalloc(datasize);
		if (mod->symbol_data == NULL)
			goto bad;
		mod->allocated |= 2;
	}

	mod->unwind_data = _stp_kmalloc(unwindsize);
	if (mod->unwind_data == NULL) {
		mod->unwind_data = _stp_vmalloc(unwindsize);
		if (mod->unwind_data == NULL)
			goto bad;
		mod->allocated |= 4;
	}
	
	mod->num_symbols = num;
	return mod;

bad:
	if (mod) {
		if (mod->symbols) {
			if (mod->allocated & 1)
				_stp_vfree(mod->symbols);
			else
				_stp_kfree(mod->symbols);
			mod->symbols = NULL;
		}
		if (mod->symbol_data) {
			if (mod->allocated & 2)
				_stp_vfree(mod->symbol_data);
			else
				_stp_kfree(mod->symbol_data);
			mod->symbol_data = NULL;
		}
		_stp_kfree(mod); 
		if (mod->symbols) {
			if (mod->allocated & 1)
				_stp_vfree(mod->symbols);
			else
				_stp_kfree(mod->symbols);
			mod->symbols = NULL;
		}
		_stp_kfree(mod); 
	}
	return NULL;
}

static struct _stp_module * _stp_alloc_module_from_module (struct module *m, uint32_t unwind_len)
{
	unsigned datasize, num = _stp_get_sym_sizes(m, &datasize);
	return _stp_alloc_module(num, datasize, unwind_len);
}

static void _stp_free_module(struct _stp_module *mod)
{
	/* The module write lock is held. Any prior readers of this */
	/* module's data will have read locks and need to finish before */
	/* the memory is freed. */
	write_lock(&mod->lock);
	write_unlock(&mod->lock); /* there will be no more readers */

	/* free symbol memory */
	if (mod->symbols) {
		if (mod->allocated & 1)
			_stp_vfree(mod->symbols);
		else
			_stp_kfree(mod->symbols);
		mod->symbols = NULL;
	}
	if (mod->symbol_data) {
		if (mod->allocated & 2)
			_stp_vfree(mod->symbol_data);
		else
			_stp_kfree(mod->symbol_data);
		mod->symbol_data = NULL;

	}
	if (mod->unwind_data) {
		if (mod->allocated & 4)
			_stp_vfree(mod->unwind_data);
		else
			_stp_kfree(mod->unwind_data);
		mod->unwind_data = NULL;

	}
	if (mod->sections) {
		_stp_kfree(mod->sections);
		mod->sections = NULL;
	}

	/* free module memory */
	_stp_kfree(mod);
}

/* Delete a module and free its memory. */
/* The module lock should already be held before calling this. */
static void _stp_del_module(struct _stp_module *mod)
{
	int i, num;

	// kbug(DEBUG_SYMBOLS, "deleting %s\n", mod->name);

	/* signal relocation code to clear its cache */
	_stp_module_relocate((char *)-1, NULL, 0);

	/* remove module from the arrays */
	for (num = 0; num < _stp_num_modules; num++) {
		if (_stp_modules[num] == mod)
			break;
	}
	if (num >= _stp_num_modules)
		return;

	for (i = num; i < _stp_num_modules-1; i++)
		_stp_modules[i] = _stp_modules[i+1];

	for (num = 0; num < _stp_num_modules; num++) {
		if (_stp_modules_by_addr[num] == mod)
			break;
	}
	for (i = num; i < _stp_num_modules-1; i++)
		_stp_modules_by_addr[i] = _stp_modules_by_addr[i+1];

	_stp_num_modules--;

	_stp_free_module(mod);
}

static void _stp_free_modules(void)
{	
	int i;
	unsigned long flags;

	/* This only happens when the systemtap module unloads */
	/* so there is no need for locks. */
	for (i = _stp_num_modules - 1; i >= 0; i--)
		_stp_del_module(_stp_modules[i]);
}

static unsigned long _stp_kallsyms_lookup_name(const char *name);

/* process the KERNEL symbols */
static int _stp_do_symbols(const char __user *buf, int count)
{
	struct _stp_symbol *s;
	unsigned datasize, num, unwindsize;	
	int i;

	switch (_stp_symbol_state) {
	case 0:
		if (count != sizeof(struct _stp_msg_symbol_hdr)) {
			errk("count=%d\n", count);
			return -EFAULT;
		}		
		if (get_user(num, (unsigned __user *)buf))
			return -EFAULT;
		if (get_user(datasize, (unsigned __user *)(buf+4)))
			return -EFAULT;
		if (get_user(unwindsize, (unsigned __user *)(buf+8)))
			return -EFAULT;
		dbug(DEBUG_UNWIND, "num=%d datasize=%d unwindsize=%d\n", num, datasize, unwindsize);

		_stp_modules[0] = _stp_alloc_module(num, datasize, unwindsize);
		if (_stp_modules[0] == NULL) {
			errk("cannot allocate memory\n");
			return -EFAULT;
		}
		rwlock_init(&_stp_modules[0]->lock);
		_stp_symbol_state = 1;
		break;
	case 1:
		dbug(DEBUG_SYMBOLS, "got stap_symbols, count=%d\n", count);
		if (copy_from_user ((char *)_stp_modules[0]->symbols, buf, count))
			return -EFAULT;
		_stp_symbol_state = 2;
		break;
	case 2:
		dbug(DEBUG_SYMBOLS, "got symbol data, count=%d buf=%p\n", count, buf);
		if (copy_from_user (_stp_modules[0]->symbol_data, buf, count))
			return -EFAULT;
		_stp_num_modules = 1;
		
		s = _stp_modules[0]->symbols;
		for (i = 0; i < _stp_modules[0]->num_symbols; i++) 
			s[i].symbol += (long)_stp_modules[0]->symbol_data;

		_stp_symbol_state = 3;
                /* NB: this mapping is used by kernel/_stext pseudo-relocations. */
		_stp_modules[0]->text = _stp_kallsyms_lookup_name("_stext");
		_stp_modules[0]->data = _stp_kallsyms_lookup_name("_etext");
		_stp_modules[0]->text_size = _stp_modules[0]->data - _stp_modules[0]->text;		
		_stp_modules_by_addr[0] = _stp_modules[0];
		dbug(DEBUG_SYMBOLS, "Got kernel symbols. text=%p len=%u\n", 
		     (int64_t)_stp_modules[0]->text, _stp_modules[0]->text_size);
		break;
	case 3:
		dbug(DEBUG_UNWIND, "got unwind data, count=%d\n", count);
		_stp_symbol_state = 4;
		if (copy_from_user (_stp_modules[0]->unwind_data, buf, count)) {
			_dbug("cfu failed\n");
			return -EFAULT;
		}
		_stp_modules[0]->unwind_data_len = count;
		break;
	default:
		errk("unexpected symbol data of size %d.\n", count);
	}
	return count;
}

static int _stp_compare_addr(const void *p1, const void *p2)
{
	struct _stp_symbol *s1 = (struct _stp_symbol *)p1;
	struct _stp_symbol *s2 = (struct _stp_symbol *)p2;
	if (s1->addr == s2->addr) return 0;
	if (s1->addr < s2->addr) return -1;
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
	int (*cmp)(const void *, const void *),
	void (*swap)(void *, void *, int size))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size, n = num * size, c, r;

	if (!swap)
		swap = (size == 4 ? u32_swap : generic_swap);

	/* heapify */
	for ( ; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r  = c) {
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

/* Create a new _stp_module and load the symbols */
static struct _stp_module *_stp_load_module_symbols (struct _stp_module *imod, uint32_t unwind_len)
{
	unsigned i, num=0;
	struct module *m = (struct module *)imod->module;
	struct _stp_module *mod = NULL;
	char *dataptr;

	if (m == NULL) {
		kbug(DEBUG_SYMBOLS, "imod->module is NULL\n");
		return NULL;
	}
	if (try_module_get(m)) {

		mod = _stp_alloc_module_from_module(m, unwind_len);
		if (mod == NULL) {
			module_put(m);
			errk("failed to allocate memory for module.\n");
			return NULL;
		}

		strlcpy(mod->name, imod->name, STP_MODULE_NAME_LEN);
		mod->module = imod->module;
		mod->text = imod->text;
		mod->data = imod->data;
		mod->num_sections = imod->num_sections;
		mod->sections = imod->sections;
		mod->text_size = m->core_text_size;
		rwlock_init(&mod->lock);

		/* now copy all the symbols we are interested in */
		dataptr = mod->symbol_data;
		for (i=0; i < m->num_symtab; i++) {
			char *str = (char *)(m->strtab + m->symtab[i].st_name);
			if (*str != '\0' && _stp_sym_type_ok(m->symtab[i].st_info)) {
				mod->symbols[num].symbol = dataptr;
				mod->symbols[num].addr = m->symtab[i].st_value;
				while (*str) *dataptr++ = *str++;
				*dataptr++ = 0;
				num++;
			}
		}
		module_put(m);

		/* sort symbols by address */
		_stp_sort (mod->symbols, num, sizeof(struct _stp_symbol), _stp_compare_addr, _stp_swap_symbol);
	}
	return mod;
}

/* Remove any old module info from our database */
static void _stp_module_exists_delete (struct _stp_module *mod)
{
	int i, num;

	/* remove any old modules with the same name */
	for (num = 1; num < _stp_num_modules; num++) {
		if (strcmp(_stp_modules[num]->name, mod->name) == 0) {
			dbug(DEBUG_SYMBOLS, "found existing module with name %s. Deleting.\n", mod->name);
			_stp_del_module(_stp_modules[num]);
			break;
		}
	}

	/* remove modules with overlapping addresses */
	for (num = 1; num < _stp_num_modules; num++) {
		if (mod->text + mod->text_size < _stp_modules_by_addr[num]->text)
			continue;
		if (mod->text < _stp_modules_by_addr[num]->text 
		    + _stp_modules_by_addr[num]->text_size) {
			dbug(DEBUG_SYMBOLS, "New module %s overlaps with old module %s. Deleting old.\n", 
			     mod->name, _stp_modules_by_addr[num]->name);
			_stp_del_module(_stp_modules_by_addr[num]);
		}
	}

}

static int _stp_ins_module(struct _stp_module *mod)
{
	int i, num, res, ret = 0;
	unsigned long flags;

	// kbug(DEBUG_SYMBOLS, "insert %s\n", mod->name);

	STP_WLOCK_MODULES;

	_stp_module_exists_delete(mod);

	/* check for overflow */
	if (_stp_num_modules == STP_MAX_MODULES) {
		errk("Exceeded the limit of %d modules\n", STP_MAX_MODULES);
		ret = -ENOMEM;
		goto done;
	}
	
	/* insert alphabetically in _stp_modules[] */
	for (num = 1; num < _stp_num_modules; num++)
		if (strcmp(_stp_modules[num]->name, mod->name) > 0)
			break;
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules[i] = _stp_modules[i-1];
	_stp_modules[num] = mod;

	/* insert by text address in _stp_modules_by_addr[] */
	for (num = 1; num < _stp_num_modules; num++)
		if (mod->text < _stp_modules_by_addr[num]->text)
			break;
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules_by_addr[i] = _stp_modules_by_addr[i-1];
	_stp_modules_by_addr[num] = mod;
	
	_stp_num_modules++;

done:
	STP_WUNLOCK_MODULES;
	return ret;
}


/* Called from procfs.c when a STP_MODULE msg is received */
static int _stp_do_module(const char __user *buf, int count)
{
	struct _stp_msg_module tmpmod;
	struct _stp_module mod, *m;
	unsigned i, section_len;

	if (count < (int)sizeof(tmpmod)) {
		errk("expected %d and got %d\n", (int)sizeof(tmpmod), count);
		return -EFAULT;
	}
	if (copy_from_user ((char *)&tmpmod, buf, sizeof(tmpmod)))
		return -EFAULT;

	section_len = count - sizeof(tmpmod) - tmpmod.unwind_len;
	if (section_len <= 0) {
		errk("section_len = %d\n", section_len);
		return -EFAULT;
	}
	dbug(DEBUG_SYMBOLS, "Got module %s, count=%d section_len=%d unwind_len=%d\n", 
	     tmpmod.name, count, section_len, tmpmod.unwind_len);

	strcpy(mod.name, tmpmod.name);
	mod.module = tmpmod.module;
	mod.text = tmpmod.text;
	mod.data = tmpmod.data;
	mod.num_sections = tmpmod.num_sections;
	
	/* copy in section data */
	mod.sections = _stp_kmalloc(section_len);
	if (mod.sections == NULL) {
		errk("unable to allocate memory.\n");
		return -EFAULT;
	}
	if (copy_from_user ((char *)mod.sections, buf+sizeof(tmpmod), section_len)) {
		_stp_kfree(mod.sections);
		return -EFAULT;
	}
	for (i = 0; i < mod.num_sections; i++) {
		mod.sections[i].symbol =  
			(char *)((long)mod.sections[i].symbol 
				 + (long)((long)mod.sections + mod.num_sections * sizeof(struct _stp_symbol)));
	}

	#if 0
	for (i = 0; i < mod.num_sections; i++)
		_dbug("section %d (stored at %p): %s %lx\n", i, &mod.sections[i], mod.sections[i].symbol, mod.sections[i].addr);
	#endif

	/* load symbols from tmpmod.module to mod */
	m = _stp_load_module_symbols(&mod, tmpmod.unwind_len); 
	if (m == NULL) {
		_stp_kfree(mod.sections);
		return 0;
	}

	dbug(DEBUG_SYMBOLS, "module %s loaded.  Text=%p text_size=%u\n", m->name, (int64_t)m->text, m->text_size);
	/* finally copy unwind info */
	if (copy_from_user (m->unwind_data, buf+sizeof(tmpmod)+section_len, tmpmod.unwind_len)) {
		_stp_free_module(m);
		_stp_kfree(mod.sections);
		return -EFAULT;
	}
	m->unwind_data_len = tmpmod.unwind_len;

	if (_stp_ins_module(m) < 0) {
		_stp_free_module(m);
		return -ENOMEM;
	}
	
	return count;
}

static int _stp_ctl_send (int type, void *data, int len);

static int _stp_module_load_notify(struct notifier_block * self, unsigned long val, void * data)
{
	struct module *mod = (struct module *)data;
	struct _stp_module rmod;

	switch (val) {
	case MODULE_STATE_COMING:
		dbug(DEBUG_SYMBOLS, "module %s load notify\n", mod->name);
		strlcpy(rmod.name, mod->name, STP_MODULE_NAME_LEN);
		_stp_ctl_send(STP_MODULE, &rmod, sizeof(struct _stp_module));
		break;
	default:
		errk("module loaded? val=%ld\n", val);
	}
	return 0;
}

static struct notifier_block _stp_module_load_nb = {
	.notifier_call = _stp_module_load_notify,
};

#endif /* _SYMBOLS_C_ */
