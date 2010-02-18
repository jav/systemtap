/* uprobes_lib test case
 * Copyright (C) 2009, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "sys/sdt.h"

// function from our library
int lib_main (void);

// volatile static variable to prevent folding of main_func
static volatile int bar;

// Marked noinline and has an empty asm statement to prevent inlining
// or optimizing away totally.
int
__attribute__((noinline))
main_func (int foo)
{
  asm ("");
  STAP_PROBE1(test, main_count, foo);
  if (foo - bar > 0)
    bar = main_func (foo - bar);
  else
    lib_main();
  return bar;
}

int
main (int argc, char *argv[], char *envp[])
{
  bar = 1;
  bar = main_func (3);
  return 0;
}
