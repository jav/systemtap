/* -*- linux-c -*-
 * ia64 stack tracing functions
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

struct dump_para{
	unsigned long *sp;
	String str;
};

static void __stp_show_stack_sym(struct unw_frame_info *info, void *arg)
{
	unsigned long ip, skip=1;
	String str = ((struct dump_para*)arg)->str;
	struct pt_regs *regs = container_of(((struct dump_para*)arg)->sp, struct pt_regs, r12);

	do {
		unw_get_ip(info, &ip);
		if (ip == 0) break;
                if (skip){
			if (ip == REG_IP(regs))
				skip = 0;
                        else continue;
                }
		_stp_string_cat(str, " ");
		_stp_symbol_sprint(str, ip);
		_stp_string_cat (str, "\n");
        } while (unw_unwind(info) >= 0);
}

static void __stp_show_stack_addr(struct unw_frame_info *info, void *arg)
{
	unsigned long ip, skip=1;
	String str = ((struct dump_para*)arg)->str;
	struct pt_regs *regs = container_of(((struct dump_para*)arg)->sp, struct pt_regs, r12);	

	do {
		unw_get_ip(info, &ip);
		if (ip == 0) break;
		if (skip){
			if (ip == REG_IP(regs))
				skip = 0;
			continue;
		}
		_stp_sprintf (str, "%lx ", ip);
	} while (unw_unwind(info) >= 0);
}

static void __stp_stack_sprint (String str, struct pt_regs *regs, int verbose, int levels)
{
	unsigned long *stack = (unsigned long *)&REG_SP(regs);
	struct dump_para para;
	
	para.str = str;
	para.sp  = stack; 
	if (verbose)
		unw_init_running(__stp_show_stack_sym, &para);
        else
		unw_init_running(__stp_show_stack_addr, &para);
}
