/* uprobes_lib test case - library helper
 * Copyright (C) 2009, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "sys/sdt.h"

// volatile static variable to prevent folding of lib_func
static volatile int foo;

// Marked noinline and has an empty asm statement to prevent inlining
// or optimizing away totally.
int
__attribute__((noinline))
lib_func (int bar)
{
  asm ("");
  STAP_PROBE1(test, func_count, bar);
  if (bar - foo > 0)
    foo = lib_func (bar - foo);
  return foo;
}

void
lib_main ()
{
  foo = 1;
  foo = lib_func (3);
}
