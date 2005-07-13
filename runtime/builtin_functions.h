#ifndef _BUILTIN_FUNCTIONS_
#define _BUILTIN_FUNCTIONS_

/* Builtin function definitions.
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/* 
 * This file contains definitions for "builtin functions" which are
 * registered in the systemtap translator, but given no definition.
 * The translator emits calls to builtins exactly the same way it
 * emits calls to functions written in the systemtap language; the
 * only difference is that this file (or a C tapset) must supply the
 * definition.
 *
 * Every builtin function "foo" called by a systemtap script is
 * translated to a C call to a C function named "function_foo". This
 * is the function you must provide in this file. In addition, the
 * translator emits a 
 * 
 *  #define _BUILTIN_FUNCTION_foo_ 
 *
 * symbol for each such function called, which you can use to elide
 * function definitions which are not used by a given systemtap
 * script.
 */

#ifdef _BUILTIN_FUNCTION_printk_
static void
function_printk (struct context *c)
{
  printk (KERN_INFO, c->locals[c->nesting].function_printk.message);
}
#endif /* _BUILTIN_FUNCTION_printk_ */


#ifdef _BUILTIN_FUNCTION_log_
static void
function_log (struct context *c)
{
  _stp_log (c->locals[c->nesting].function_log.message);
}
#endif /* _BUILTIN_FUNCTION_log_ */


#ifdef _BUILTIN_FUNCTION_warn_
static void
function_warn (struct context *c)
{
  _stp_warn (c->locals[c->nesting].function_warn.message);
}
#endif /* _BUILTIN_FUNCTION_warn_ */


#endif /* _BUILTIN_FUNCTIONS_ */
