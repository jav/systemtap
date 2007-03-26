/* -*- linux-c -*- 
 * symbols.c - stp symbol and module functions
 *
 * Copyright (C) Red Hat Inc, 2006, 2007
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _SYMBOLS_C_
#define _SYMBOLS_C_
#include "../sym.h"
#include <linux/sort.h>

spinlock_t _stp_module_lock = SPIN_LOCK_UNLOCKED;
#define STP_TRYLOCK_MODULES  ({						\
		int numtrylock = 0;					\
		while (!spin_trylock_irqsave (&_stp_module_lock, flags) && (++numtrylock < MAXTRYLOCK)) \
			ndelay (TRYLOCKDELAY);				\
		(numtrylock >= MAXTRYLOCK);				\
			})
#define STP_LOCK_MODULES  spin_lock_irqsave(&_stp_module_lock, flags)
#define STP_UNLOCK_MODULES spin_unlock_irqrestore(&_stp_module_lock, flags)

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
static struct _stp_module * _stp_alloc_module(unsigned num, unsigned datasize)
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
	mod->num_symbols = num;
	return mod;

bad:
	if (mod) {
		if (mod->allocated && mod->symbols)
			vfree(mod->symbols);
		else
			kfree(mod->symbols);
		kfree(mod); 
	}
	return NULL;
}

static struct _stp_module * _stp_alloc_module_from_module (struct module *m)
{
	unsigned datasize, num = _stp_get_sym_sizes(m, &datasize);
	return _stp_alloc_module(num, datasize);
}

/* Delete a module and free its memory. */
/* The lock should already be held before calling this. */
static void _stp_del_module(struct _stp_module *mod)
{
	int i, num;

	// kbug("deleting %s\n", mod->name);

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
	
	/* free symbol memory */
	if (mod->num_symbols) {
		if (mod->allocated & 1)
			vfree(mod->symbols);
		else
			kfree(mod->symbols);
		if (mod->allocated & 2)
			vfree(mod->symbol_data);
		else
			kfree(mod->symbol_data);
	}
	if (mod->sections)
		kfree(mod->sections);

	/* free module memory */
	kfree(mod);
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
	unsigned i, datasize, num;
	struct _stp_symbol *s;

	switch (_stp_symbol_state) {
	case 0:
		if (count != 8) {
			errk(" _stp_do_symbols: count=%d\n", count);
			return -EFAULT;
		}		
		if (get_user(num, (unsigned __user *)buf))
			return -EFAULT;
		if (get_user(datasize, (unsigned __user *)(buf+4)))
			return -EFAULT;
		// kbug("num=%d datasize=%d\n", num, datasize);

		_stp_modules[0] = _stp_alloc_module(num, datasize);
		if (_stp_modules[0] == NULL) {
			errk("cannot allocate memory\n");
			return -EFAULT;
		}
		_stp_symbol_state = 1;
		break;
	case 1:
		if (copy_from_user ((char *)_stp_modules[0]->symbols, buf, count))
			return -EFAULT;
		// kbug("got stap_symbols, count=%d\n", count);
		_stp_symbol_state = 2;
		break;
	case 2:
		if (copy_from_user (_stp_modules[0]->symbol_data, buf, count))
			return -EFAULT;
		// kbug("got symbol data, count=%d\n", count);
		_stp_num_modules = 1;

		
		s = _stp_modules[0]->symbols;
		for (i = 0; i < _stp_modules[0]->num_symbols; i++) 
			s[i].symbol += (long)_stp_modules[0]->symbol_data;
		_stp_symbol_state = 3;
                /* NB: this mapping is used by kernel/_stext pseudo-relocations. */
		_stp_modules[0]->text = _stp_kallsyms_lookup_name("_stext");
		_stp_modules_by_addr[0] = _stp_modules[0];
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


/* Create a new _stp_module and load the symbols */
static struct _stp_module *_stp_load_module_symbols (struct _stp_module *imod)
{
	unsigned i, num=0;
	struct module *m = (struct module *)imod->module;
	struct _stp_module *mod = NULL;
	char *dataptr;

	if (m == NULL) {
		kbug("imod->module is NULL\n");
		return NULL;
	}
	if (try_module_get(m)) {

		mod = _stp_alloc_module_from_module(m);
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
		sort (mod->symbols, num, sizeof(struct _stp_symbol), _stp_compare_addr, _stp_swap_symbol);
	}
	return mod;
}

/* Do we already have this module? */
static int _stp_module_exists(struct _stp_module *mod)
{
	int i, res;
	unsigned long flags;
	// kbug("exists? %s\n", mod->name);
	STP_LOCK_MODULES;
	for (i = 1; i < _stp_num_modules; i++) {
		res = strcmp(_stp_modules[i]->name, mod->name);
		if (res > 0)
			break;
		if (res == 0 && _stp_modules[i]->module == mod->module) {
			STP_UNLOCK_MODULES;
			return 1;
		}
	}
	STP_UNLOCK_MODULES;
	return 0;
}

static void _stp_ins_module(struct _stp_module *mod)
{
	int i, num, res;
	unsigned long flags;

	// kbug("insert %s\n", mod->name);

	STP_LOCK_MODULES;

	/* insert alphabetically in _stp_modules[] */
	for (num = 1; num < _stp_num_modules; num++) {
		res = strcmp(_stp_modules[num]->name, mod->name);
		if (res < 0)
			continue;
		if (res > 0)
			break;
		_stp_del_module(_stp_modules[num]);
		break;
	}
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules[i] = _stp_modules[i-1];
	_stp_modules[num] = mod;

	/* insert by text address in _stp_modules_by_addr[] */
	for (num = 1; num < _stp_num_modules; num++) {
		if (_stp_modules_by_addr[num]->text > mod->text)
			break;
	}
	for (i = _stp_num_modules; i > num; i--)
		_stp_modules_by_addr[i] = _stp_modules_by_addr[i-1];
	_stp_modules_by_addr[num] = mod;

	_stp_num_modules++;

	STP_UNLOCK_MODULES;
}


/* Called from procfs.c when a STP_MODULE msg is received */
static int _stp_do_module(const char __user *buf, int count)
{
	struct _stp_module tmpmod, *mod;
	unsigned i;

	if (count < (int)sizeof(tmpmod)) {
		errk("expected %d and got %d\n", (int)sizeof(tmpmod), count);
		return -EFAULT;
	}
	if (copy_from_user ((char *)&tmpmod, buf, sizeof(tmpmod)))
		return -EFAULT;

	// kbug("Got module %s, count=%d(0x%x)\n", tmpmod.name, count,count);

	if (_stp_module_exists(&tmpmod))
		return count;

	/* copy in section data */
	tmpmod.sections = _stp_kmalloc(count - sizeof(tmpmod));
	if (tmpmod.sections == NULL) {
		errk("unable to allocate memory.\n");
		return -EFAULT;
	}
	if (copy_from_user ((char *)tmpmod.sections, buf+sizeof(tmpmod), count-sizeof(tmpmod))) {
		kfree(tmpmod.sections);
		return -EFAULT;
	}
	for (i = 0; i < tmpmod.num_sections; i++) {
		tmpmod.sections[i].symbol =  
			(char *)((long)tmpmod.sections[i].symbol 
				 + (long)((long)tmpmod.sections + tmpmod.num_sections * sizeof(struct _stp_symbol)));
	}

	#ifdef DEBUG_SYMBOLS
	for (i = 0; i < tmpmod.num_sections; i++)
		printk("section %d (stored at %p): %s %lx\n", i, &tmpmod.sections[i], tmpmod.sections[i].symbol, tmpmod.sections[i].addr);
	#endif

	/* load symbols from tmpmod.module to mod */
	mod = _stp_load_module_symbols(&tmpmod);	
	if (mod == NULL) {
		kfree(tmpmod.sections);
		return 0;
	}

	_stp_ins_module(mod);
	
	return count;
}

static int _stp_ctl_send (int type, void *data, int len);

static int _stp_module_load_notify(struct notifier_block * self, unsigned long val, void * data)
{
#ifdef CONFIG_MODULES
	struct module *mod = (struct module *)data;
	struct _stp_module rmod;

	switch (val) {
	case MODULE_STATE_COMING:
		dbug("module %s loaded\n", mod->name);
		strlcpy(rmod.name, mod->name, STP_MODULE_NAME_LEN);
		_stp_ctl_send(STP_MODULE, &rmod, sizeof(struct _stp_module));
		break;
	default:
		errk("module loaded? val=%ld\n", val);
	}
#endif
	return 0;
}

static struct notifier_block _stp_module_load_nb = {
	.notifier_call = _stp_module_load_notify,
};

#endif /* _SYMBOLS_C_ */
