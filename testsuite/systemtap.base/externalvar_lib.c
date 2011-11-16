/* external var test case - library helper
 * Copyright (C) 2009, Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Tests that an external exported variable can be accessed.
 */

#include <stdlib.h>

struct libstruct
{
  int i;
  long l;
  char c;
  struct libstruct *s1;
  struct libstruct *s2;
};

int libvar;
struct libstruct *lib_s;

static int stat_libvar;
static struct libstruct *stat_lib_s;

static void
lib_call ()
{
  asm(""); // dummy method, just to probe and extract.
}

void
lib_main ()
{
  libvar = 42;
  stat_libvar = libvar;
  lib_s = (struct libstruct *) malloc(sizeof(struct libstruct));
  lib_s->i = 1;
  lib_s->l = 2;
  lib_s->c = 3;
  lib_s->s1 = lib_s;
  lib_s->s2 = NULL;
  stat_lib_s = lib_s;
  lib_call ();
}
