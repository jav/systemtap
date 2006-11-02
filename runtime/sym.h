/*
 * Copyright (C) 2005, 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAP_SYMBOLS_H_
#define _STAP_SYMBOLS_H_

#define STP_MODULE_NAME_LEN 64

struct _stp_symbol {
  unsigned long addr;
  const char *symbol;
};

struct _stp_module {
  /* the module name, or "" for kernel */
  char name[STP_MODULE_NAME_LEN];

  /* A pointer to the struct module. Note that we cannot */
  /* trust this because as of 2.6.19, there are not yet */
  /* any notifier hooks that will tell us when a module */
  /* is unloading. */
  unsigned long module;

  /* the start of the module's text and data sections */
  unsigned long text;
  unsigned long data;

  /* how many symbols this module has that we are interested in */
  unsigned num_symbols;

  /* how many sections this module has */
  unsigned num_sections;
  struct _stp_symbol *sections;

  /* how the symbol_data below was allocated */
  int allocated;  /* 0 = kmalloc, 1 = vmalloc */
  
  /* an array of num_symbols _stp_symbol structs */
  struct _stp_symbol *symbols; /* ordered by address */

  /* where we stash our copy of the strtab */
  void *symbol_data; /* private */
};

#ifndef STP_MAX_MODULES
#define STP_MAX_MODULES 128
#endif

/* the alphabetical array of modules */
struct _stp_module *_stp_modules[STP_MAX_MODULES];

/* the array of modules ordered by addresses */
struct _stp_module *_stp_modules_by_addr[STP_MAX_MODULES];

/* the number of modules in the arrays */
int _stp_num_modules = 0;

#endif /* _STAP_SYMBOLS_H_ */
