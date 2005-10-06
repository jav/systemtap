/*
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAP_SYMBOLS_H_
#define _STAP_SYMBOLS_H_


/* A symbol table defined by the translator. */
struct stap_symbol {
  unsigned long addr;
  const char *symbol;
  const char *modname; 
};

extern struct stap_symbol stap_symbols [];
extern unsigned stap_num_symbols;


#endif /* _RUNTIME_H_ */
