/* -*- linux-c -*- 
 * Systemtap Test Module 1
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/compiler.h>
#include <asm/uaccess.h>

/* The purpose of this module is to provide a bunch of functions that */
/* do nothing important, and then call them in different contexts. */
/* We use a /proc file to trigger function calls from user context. */
/* Then systemtap scripts set probes on the functions and run tests */
/* to see if the expected output is received. This is better than using */
/* the kernel because kernel internals frequently change. */

/** These functions are in module 2 **/
/* They are there to prevent compiler optimization from */
/* optimizing away functions. */
int yyy_func1 (int);
int yyy_int (int,int,int);
unsigned yyy_uint (unsigned,unsigned,unsigned);
long yyy_long (long,long,long);
int64_t yyy_int64 (int64_t,int64_t,int64_t);
char yyy_char(char, char, char);
char *yyy_str(char *, char *, char *);

/************ Below are the functions to create this module ************/

static struct proc_dir_entry *stm_ctl = NULL;

static ssize_t stm_write_cmd (struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
  char type;

  if (get_user(type, (int __user *)buf))
	  return -EFAULT;

  switch (type) {
  case '0':
	  yyy_func1(1234);
	  break;
  case '1':
	  yyy_int(-1, 200, 300);
	  break;
  case '2':
	  yyy_uint((unsigned)-1, 200, 300);
	  break;
  case '3':
	  yyy_long(-1L, 200L, 300L);
	  break;
  case '4':
	  yyy_int64(-1, 200, 300);
	  break;
  case '5':
	  yyy_char('a', 'b', 'c');
	  break;
  case '6':
	  yyy_str("Hello", "System", "Tap");
	  break;
  default:
	  printk ("systemtap_bt_test_module: invalid command type %d\n", type);
	  return -EINVAL;
  }
  
  return count;
}

static struct file_operations stm_fops_cmd = {
	.owner = THIS_MODULE,
	.write = stm_write_cmd,
};

int init_module(void)
{

	stm_ctl = create_proc_entry ("stap_test_cmd", 0666, NULL);
	if (stm_ctl == NULL) 
		return -1;;
	stm_ctl->proc_fops = &stm_fops_cmd;
	return 0;
}

void cleanup_module(void)
{
	if (stm_ctl)
		remove_proc_entry ("stap_test_cmd", NULL);
}

MODULE_DESCRIPTION("systemtap backtrace test module1");
MODULE_LICENSE("GPL");
