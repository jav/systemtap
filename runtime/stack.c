#ifndef _STACK_C_ /* -*- linux-c -*- */
#define _STACK_C_


/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 * Without frames the best that can be done here is to scan the stack and
 * display everything that fits in the range of a valid IP. Things like function pointers
 * on the stack will certainly result in bogus addresses in the backtrace.
 *
 * With debug info, we could get a proper backtrace, but it would be too slow to do
 * during a probe.  We can eventually make this a postprocessing feature.
 *
 * @{
 */

#include "sym.c"
#include "regs.h"

#if defined (__x86_64__)

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose) {
				_stp_string_cat(str, " ");
				_stp_symbol_sprint (str, addr);
				_stp_string_cat (str, "\n");
			} else 
				_stp_sprintf (str, " 0x%lx\n", addr);
		}
	}
}

#elif  defined (__i386__)

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

static inline unsigned long print_context_stack(String str, struct thread_info *tinfo,
				unsigned long *stack, unsigned long ebp)
{
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		_stp_string_cat(str, " ");
		_stp_symbol_sprint (str, addr);
		_stp_string_cat(str, "\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			_stp_string_cat(str, " ");
			_stp_symbol_sprint(str, addr);
			_stp_string_cat(str, "\n");
		}
	}
#endif
	return ebp;
}

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long ebp;

	/* Grab ebp right from our regs */
	asm ("movl %%ebp, %0" : "=r" (ebp) : );

	while (1) {
		struct thread_info *context;
		context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = print_context_stack(str, context, stack, ebp);
		stack = (unsigned long*)context->previous_esp;
		if (!stack)
			break;
	}
}


#else
#error "Unsupported architecture"
#endif


/** Writes stack backtrace to a String
 *
 * @param str String
 * @param regs A pointer to the struct pt_regs.
 * @returns Same String as was input with trace info appended,
 */
String _stp_stack_sprint (String str, struct pt_regs *regs)
{
	_stp_sprintf (str, "trace for %d (%s)\n ", current->pid, current->comm);
	_stp_symbol_sprint (str, REG_IP(regs));
	_stp_string_cat(str, "\n");
	__stp_stack_sprint (str, (unsigned long *)&REG_SP(regs), 1, 0);
	return str;
}

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 * @note Calls _stp_print_flush().
 */

#define _stp_stack_print(regs)					\
	{							\
		(void)_stp_stack_sprint(_stp_stdout,regs);	\
		_stp_print_flush();				\
	}

/** Writes stack backtrace to a String.
 * Use this when calling from a jprobe.
 * @param str String
 * @returns Same String as was input with trace info appended,
 * @sa _stp_stack_sprint()
 */
String _stp_stack_sprintj(String str)
{
	unsigned long stack;
	_stp_sprintf (str, "trace for %d (%s)\n", current->pid, current->comm);
	__stp_stack_sprint (str, &stack, 1, 0);
	return str;
}

/** Prints the stack backtrace.
 * Use this when calling from a jprobe.
 * @sa _stp_stack_print()
 * @note Calls _stp_print_flush().
 */
#define _stp_stack_printj()					\
	{							\
		(void)_stp_stack_sprintj(_stp_stdout);		\
		_stp_print_flush();				\
	}

/** Writes the user stack backtrace to a String
 * @param str String
 * @returns Same String as was input with trace info appended,
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 */
String _stp_ustack_sprint (String str)
{
	struct pt_regs *nregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) current->thread_info)) - 1;
#if BITS_PER_LONG == 64
	_stp_sprintf (str, " 0x%016lx : [user]\n", REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_sprintf (str, " 0x%016lx : [user]\n", *(unsigned long *)REG_SP(nregs));
#else
	_stp_sprintf (str, " 0x%08lx : [user]\n", REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_sprintf (str, " 0x%08lx : [user]\n", *(unsigned long *)REG_SP(nregs));
#endif
	return str;
}

/** Prints the user stack backtrace
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 * Calls _stp_print_flush().
 */
#define _stp_ustack_print()					\
	{							\
		(void)_stp_ustack_sprint(_stp_stdout);		\
		_stp_print_flush();				\
	}

/** @} */
#endif /* _STACK_C_ */
