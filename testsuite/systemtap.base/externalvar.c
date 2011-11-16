/* externalvar test case
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

// function from our library
int lib_main (void);

struct exestruct
{
  char c;
  int i;
  long l;
  struct exestruct *s1;
  struct exestruct *s2;
};

char exevar_c;
int exevar_i;
long exevar_l;
struct exestruct *exe_s;

static char stat_exevar_c;
static int stat_exevar_i;
static long stat_exevar_l;
static struct exestruct *stat_exe_s;

static void
main_call ()
{
  asm (""); // dummy method, just to probe and extract and jump into lib.
  lib_main ();
}

int
main ()
{
  exevar_c = 42;
  stat_exevar_c = exevar_c;
  exevar_i = 2;
  stat_exevar_i = exevar_i;
  exevar_l = 21;
  stat_exevar_l = exevar_l;
  exe_s = (struct exestruct *) malloc(sizeof(struct exestruct));
  exe_s->i =1;
  exe_s->l =2;
  exe_s->c =3;
  exe_s->s1 = NULL;
  exe_s->s2 = exe_s;
  stat_exe_s = exe_s;
  main_call ();
  return 0;
}
