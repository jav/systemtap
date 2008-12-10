/* usymbol test case
 * Copyright (C) 2008, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Uses signal to tranfer user space addresses into the kernel where a
 * probe on sigaction will extract them and produce the symbols.  To
 * poke into the executable we get the sa_handler, to poke into glibc
 * we get the sa_restorer fields in the stap script.
 *
 * XXX - Seems sa_restorer isn't set on all architectures.  should use
 * our own shared library and set signal handler from there.  Also
 * need to handle @plt symbols (setting a handler in the main
 * executable that is in a shared library will have the @plt address,
 * not the address inside the shared library).
 */

#include <signal.h>
typedef void (*sighandler_t)(int);

void
handler (int signum)
{
  /* dummy handler, just used for the address... */
}

sighandler_t
libc_handler (void *func)
{
  return (sighandler_t) func;
}

int
main (int argc, char *argv[], char *envp[])
{
  // Use SIGFPE since we never expect that to be triggered.
  signal(SIGFPE, handler);
}
