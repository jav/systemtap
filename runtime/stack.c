#ifndef _STACK_C_
#define _STACK_C_
/* -*- linux-c -*- */

/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 * @{
 */

#include "sym.c"

static int (*_stp_kta)(unsigned long addr)=(void *)KTA;

#ifdef __x86_64__
static void __stp_stack_print (unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;

	if (verbose)
		_stp_print ("trace for %d (%s)\n", current->pid, current->comm);

	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose)
				_stp_symbol_print (addr);
			else
				_stp_print ("0x%lx ", addr);
		}
	}
	_stp_print_str ("\n");
}


static char *__stp_stack_sprint (unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;
	char *ptr = _stp_scbuf_cur();
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose)
				_stp_symbol_sprint (addr);
			else
				_stp_sprint ("0x%lx ", addr);
		}
	}
	return ptr;
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
		_stp_print_str("\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta (addr)) {
			_stp_symbol_print (addr);
			_stp_print_str ("\n");
		}
	}
#endif
	return ebp;
}

static inline unsigned long _stp_sprint_context_stack (
	struct thread_info *tinfo,
	unsigned long *stack, 
	unsigned long ebp )
{
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		_stp_symbol_sprint (addr);
		_stp_sprint_str("\n");
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta (addr)) {
			_stp_symbol_sprint (addr);
			_stp_sprint_str ("\n");
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
}

static void __stp_stack_sprint (unsigned long *stack, int verbose, int levels)
{
	unsigned long ebp;

	/* Grab ebp right from our regs */
	asm ("movl %%ebp, %0" : "=r" (ebp) : );

	while (stack) {
		struct thread_info *context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = _stp_sprint_context_stack (context, stack, ebp);
		stack = (unsigned long*)context->previous_esp;
	}
}

#endif /* i386 */

void _stp_stack_print (int verbose, int levels)
{
  unsigned long stack;
  return __stp_stack_print (&stack, verbose, levels);
}

char *_stp_stack_sprint (int verbose, int levels)
{
  unsigned long stack;
  char *ptr = _stp_scbuf_cur();
  __stp_stack_sprint (&stack, verbose, levels);
  return ptr;
}

/** @} */
#endif /* _STACK_C_ */
