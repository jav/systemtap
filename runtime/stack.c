#ifndef _STACK_C_ /* -*- linux-c -*- */
#define _STACK_C_


/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 * @{
 */

#include "sym.c"

static int (*_stp_kta)(unsigned long addr)=(void *)KTA;


struct frame_head {
	struct frame_head * ebp;
	unsigned long ret;
} __attribute__((packed));

static struct frame_head *
dump_backtrace(struct frame_head * head)
{
	_stp_printf ("db: %lx\n", head->ret);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= head->ebp)
		return NULL;

	return head->ebp;
}

static int pages_present(struct frame_head * head)
{
	struct mm_struct * mm = current->mm;

	/* FIXME: only necessary once per page */
	if (!check_user_page_readable(mm, (unsigned long)head))
		return 0;

	return check_user_page_readable(mm, (unsigned long)(head + 1));
}

static int valid_kernel_stack(struct frame_head * head, struct pt_regs * regs)
{
	unsigned long headaddr = (unsigned long)head;
	unsigned long stack = (unsigned long)regs;
	unsigned long stack_base = (stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE;
	_stp_log ("%lx %lx %lx\n", headaddr, stack, stack_base);
	return headaddr < stack_base;
}

void
x86_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct frame_head *head;

#ifdef CONFIG_X86_64
	head = (struct frame_head *)regs->rbp;
#else
	head = (struct frame_head *)regs->ebp;
#endif

	if (!user_mode(regs)) {
		_stp_log ("kernel mode\n");
		while (depth-- && valid_kernel_stack(head, regs))
			head = dump_backtrace(head);
		_stp_print_flush();
		return;
	}

#ifdef CONFIG_SMP
	if (!spin_trylock(&current->mm->page_table_lock))
		return;
#endif

	while (depth-- && head && pages_present(head))
		head = dump_backtrace(head);

#ifdef CONFIG_SMP
	spin_unlock(&current->mm->page_table_lock);
#endif
	_stp_print_flush();
}


#ifdef __x86_64__
static void __stp_stack_print (unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack;
		if (_stp_kta(addr)) {
			if (verbose) {
				_stp_symbol_print (addr);
				_stp_print ("\n");
			} else
				_stp_printf ("0x%lx ", addr);
		}
		stack++;
	}
	_stp_print_flush();
}


static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose) {
				_stp_symbol_sprint (str, addr);
				_stp_sprintf (str, "\n");
			} else
				_stp_sprintf (str, "0x%lx\n", addr);
		}
	}
}

#else  /* i386 */

static inline int valid_stack_ptr (struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

static inline unsigned long _stp_print_context_stack (
	struct thread_info *tinfo,
	unsigned long *stack, 
	unsigned long ebp )
{
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		_stp_symbol_print (addr);
		_stp_print_cstr("\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta (addr)) {
			_stp_symbol_print (addr);
			_stp_print_cstr ("\n");
		}
	}
#endif
	_stp_print_flush();
	return ebp;
}

static inline unsigned long _stp_sprint_context_stack (
	String str,
	struct thread_info *tinfo,
	unsigned long *stack, 
	unsigned long ebp )
{
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		_stp_symbol_sprint (str, addr);
		_stp_string_cat (str, "\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta (addr)) {
			_stp_symbol_sprint (str, addr);
			_stp_string_cat (str, "\n");
		}
	}
#endif
	return ebp;
}

static void __stp_stack_print (unsigned long *stack, int verbose, int levels)
{
	unsigned long ebp;

	/* Grab ebp right from our regs */
	asm ("movl %%ebp, %0" : "=r" (ebp) : );

	while (stack) {
		struct thread_info *context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = _stp_print_context_stack (context, stack, ebp);
		stack = (unsigned long*)context->previous_esp;
	}
	_stp_print_flush ();
}

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long ebp;

	/* Grab ebp right from our regs */
	asm ("movl %%ebp, %0" : "=r" (ebp) : );

	while (stack) {
		struct thread_info *context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = _stp_sprint_context_stack (str, context, stack, ebp);
		stack = (unsigned long*)context->previous_esp;
	}
}

#endif /* i386 */

/** Print stack dump.
 * Prints a stack dump to the print buffer.
 * @param verbose Verbosity
 * @param levels Number of levels to trace.
 * @todo Implement verbosity and levels parameters.
 * @bug levels parameter is not functional
 */

void _stp_stack_jprint (int verbose, int levels)
{
  unsigned long stack;
  __stp_stack_print (&stack, verbose, levels);
}

void _stp_stack_print (struct pt_regs *regs, int verbose, int levels)
{
	if (verbose) {
		_stp_printf ("trace for %d (%s)\n", current->pid, current->comm);
		_stp_symbol_print (regs->rip);
		_stp_print ("\n");
	} else
		_stp_printf ("0x%lx ", regs->rip);

	__stp_stack_print ((unsigned long *)regs->rsp, verbose, levels);
}

/** Writes stack dump to a String
 *
 * @param str String
 * @param verbose Verbosity
 * @param levels Number of levels to trace.
 * @returns Same String as was input.
 * @todo Implement verbosity and levels parameters.
 * @bug levels parameter is not functional
 */

String _stp_stack_sprint (String str, int verbose, int levels)
{
  unsigned long stack;
  __stp_stack_sprint (str, &stack, verbose, levels);
  return str;
}

/** @} */
#endif /* _STACK_C_ */
