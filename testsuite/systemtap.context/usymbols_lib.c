/* usymbol test case - library helper
 * Copyright (C) 2008, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Uses signal to tranfer user space addresses into the kernel where a
 * probe on sigaction will extract them and produce the symbols.  To
 * poke into the executable we get the sa_handler set through signal
 * from this library.
 */

#include <signal.h>
typedef void (*sighandler_t)(int);

void
lib_handler (int signum)
{
  /* dummy handler, just used for the address... */
}

void
lib_main ()
{
  // Use SIGFPE since we never expect that to be triggered.
  signal(SIGFPE, lib_handler);
}
