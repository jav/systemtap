/* uprobes_lib test case - library helper
 * Copyright (C) 2009, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

void
lib_func (int bar)
{
  if (bar > 1)
    lib_func (bar - 1);
}

void
lib_main ()
{
  lib_func (3);
}
