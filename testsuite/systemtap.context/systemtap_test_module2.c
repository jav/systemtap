/* -*- linux-c -*- 
 * Systemtap Test Module 2
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/compiler.h>

/* The purpose of this module is to provide a bunch of functions that */
/* do nothing important, and then call them in different contexts. */
/* We use a /proc file to trigger function calls from user context. */
/* Then systemtap scripts set probes on the functions and run tests */
/* to see if the expected output is received. */

/** Here are all the functions we will probe **/

/* some nested functions to test backtraces */
int noinline yyy_func4 (int foo) {
        asm ("");
        return foo + 1;
}
int noinline yyy_func3 (int foo) {
	foo = yyy_func4(foo);
        asm ("");
        return foo + 1;
}
int noinline yyy_func2 (int foo) {
        foo = yyy_func3(foo);
        asm ("");
        return foo + 1;
}

int noinline yyy_func1 (int foo) {
	foo = yyy_func2(foo);
        asm ("");
        return foo + 1;
}
EXPORT_SYMBOL(yyy_func1);

/* 1. int argument testing */
int noinline yyy_int(int a, int b, int c)
{
        asm ("");
	return a+b+c;
}
/* 2. uint argument testing */
unsigned noinline yyy_uint(unsigned a, unsigned b, unsigned c)
{
        asm ("");
	return a+b+c;
}
/* 3. long argument testing */
long noinline yyy_long(long a, long b, long c)
{
        asm ("");
	return a+b+c;
}
/* 4. int64_t argument testing */
int noinline yyy_int64(int64_t a, int64_t b, int64_t c)
{
        asm ("");
	return a+b+c;
}
/* 5. char argument testing */
char noinline yyy_char(char a, char b, char c)
{
        asm ("");
	return 'Q';
}
/* 5. string argument testing */
char * noinline yyy_str(char *a, char *b, char *c)
{
        asm ("");
	return "XYZZY";
}

EXPORT_SYMBOL(yyy_int);
EXPORT_SYMBOL(yyy_uint);
EXPORT_SYMBOL(yyy_long);
EXPORT_SYMBOL(yyy_int64);
EXPORT_SYMBOL(yyy_char);
EXPORT_SYMBOL(yyy_str);

MODULE_DESCRIPTION("systemtap backtrace test module2");
MODULE_LICENSE("GPL");
