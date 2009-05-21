/* uprobes_lib test case
 * Copyright (C) 2009, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <unistd.h>

// function from our library
int lib_main (void);

void
main_func (int foo)
{
  if (foo > 1)
    main_func (foo - 1);
  else
    lib_main();
}

int
main (int argc, char *argv[], char *envp[])
{
  main_func (3);
  return 0;
}
