/* target operations
 * Copyright (C) 2005-2010 Red Hat Inc.
 * Copyright (C) 2005, 2006, 2007 Intel Corporation.
 * Copyright (C) 2007 Quentin Barnes.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifdef STAPCONF_LINUX_UACCESS_H
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif
#include <linux/types.h>
#define intptr_t long
#define uintptr_t unsigned long


/* These three macro definitions are generic, just shorthands
   used by the generated code.  */

#define op_abs(x)	(x < 0 ? -x : x)

#define fetch_bitfield(target, base, higherbits, nbits)			      \
  target = (((base) >> (sizeof (base) * 8 - (higherbits) - (nbits)))	      \
	    & (((__typeof (base)) 1 << (nbits)) - 1))

#define store_bitfield(target, base, higherbits, nbits)			      \
  target = ((target							      \
	     &~ ((((__typeof (target)) 1 << (nbits)) - 1)		      \
		 << (sizeof (target) * 8 - (higherbits) - (nbits))))	      \
	    | ((((__typeof (target)) (base))				      \
		& (((__typeof (target)) 1 << (nbits)) - 1))		      \
	       << (sizeof (target) * 8 - (higherbits) - (nbits))))


/* Given a DWARF register number, fetch its intptr_t (long) value from the
   probe context, or store a new value into the probe context.

   The register number argument is always a canonical decimal number, so it
   can be pasted into an identifier name.  These definitions turn it into a
   per-register macro, defined below for machines with individually-named
   registers.  */
#define k_fetch_register(regno) \
  ((intptr_t) k_dwarf_register_##regno (c->regs))
#define k_store_register(regno, value) \
  (k_dwarf_register_##regno (c->regs) = (value))


/* The deref and store_deref macros are called to safely access addresses
   in the probe context.  These macros are used only for kernel addresses.
   The macros must handle bogus addresses here gracefully (as from
   corrupted data structures, stale pointers, etc), by doing a "goto
   deref_fault".

   On most machines, the asm/uaccess.h macros __get_user_asm and
   __put_user_asm do exactly the low-level work we need to access memory
   with fault handling, and are not actually specific to user-address
   access at all.  Each machine's definition of deref and deref_store here
   must work right for kernel addresses, and can use whatever existing
   machine-specific kernel macros are convenient.  */

#if STP_SKIP_BADVARS
#define DEREF_FAULT(addr) ({0; })
#define STORE_DEREF_FAULT(addr) ({0; })
#else
#define DEREF_FAULT(addr) ({						    \
    snprintf(c->error_buffer, sizeof(c->error_buffer),			    \
      "kernel read fault at 0x%p (%s)", (void *)(intptr_t)(addr), #addr);   \
    c->last_error = c->error_buffer;					    \
    goto deref_fault;							    \
    })

#define STORE_DEREF_FAULT(addr) ({					    \
    snprintf(c->error_buffer, sizeof(c->error_buffer),			    \
      "kernel write fault at 0x%p (%s)", (void *)(intptr_t)(addr), #addr);  \
    c->last_error = c->error_buffer;					    \
    goto deref_fault;							    \
    })
#endif

/* PR 10601: user-space (user_regset) register access.  */
#if defined(STAPCONF_REGSET)
#include <linux/regset.h>
#endif

#if defined(STAPCONF_UTRACE_REGSET)
#include <linux/tracehook.h>
/* adapt new names to old decls */
#define user_regset_view utrace_regset_view
#define user_regset utrace_regset
#define task_user_regset_view utrace_native_view
#endif

#if defined(STAPCONF_REGSET) || defined(STAPCONF_UTRACE_REGSET)

struct usr_regset_lut {
  char *name;
  unsigned rsn;
  unsigned pos;
};


/* DWARF register number -to- user_regset bank/offset mapping table.
   The register numbers come from the processor-specific ELF documents.
   The user-regset bank/offset values come from kernel $ARCH/include/asm/user*.h
   or $ARCH/kernel/ptrace.c. */
static const struct usr_regset_lut url_i386[] = {
  { "ax", NT_PRSTATUS, 6*4 },
  { "cx", NT_PRSTATUS, 1*4 },
  { "dx", NT_PRSTATUS, 2*4 },
  { "bx", NT_PRSTATUS, 0*4 },
  { "sp", NT_PRSTATUS, 15*4 },
  { "bp", NT_PRSTATUS, 5*4 },
  { "si", NT_PRSTATUS, 3*4 },
  { "di", NT_PRSTATUS, 4*4 },
  { "ip", NT_PRSTATUS, 12*4 },
};

static const struct usr_regset_lut url_x86_64[] = {
  { "rax", NT_PRSTATUS, 10*8 },
  { "rdx", NT_PRSTATUS, 12*8 },
  { "rcx", NT_PRSTATUS, 11*8 },
  { "rbx", NT_PRSTATUS, 5*8 },
  { "rsi", NT_PRSTATUS, 13*8 },
  { "rdi", NT_PRSTATUS, 14*8 },
  { "rbp", NT_PRSTATUS, 4*8 },
  { "rsp", NT_PRSTATUS, 19*8 },
  { "r8", NT_PRSTATUS, 9*8 },
  { "r9", NT_PRSTATUS, 8*8 }, 
  { "r10", NT_PRSTATUS, 7*8 },  
  { "r11", NT_PRSTATUS, 6*8 }, 
  { "r12", NT_PRSTATUS, 3*8 }, 
  { "r13", NT_PRSTATUS, 2*8 }, 
  { "r14", NT_PRSTATUS, 1*8 }, 
  { "r15", NT_PRSTATUS, 0*8 }, 
  { "rip", NT_PRSTATUS, 16*8 }, 
  /* XXX: SSE registers %xmm0-%xmm7 */ 
  /* XXX: SSE2 registers %xmm8-%xmm15 */
  /* XXX: FP registers %st0-%st7 */
  /* XXX: MMX registers %mm0-%mm7 */
};
/* XXX: insert other architectures here. */


static u32 ursl_fetch32 (const struct usr_regset_lut* lut, unsigned lutsize, int e_machine, unsigned regno)
{
  u32 value = ~0;
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->get)(current, rs, pos, count, & value, NULL);
        WARN_ON (rc);
        /* success */
        goto out;
      }
  WARN_ON (1); /* did not find appropriate regset! */
  
 out:
  return value;
}


static void ursl_store32 (const struct usr_regset_lut* lut,unsigned lutsize,  int e_machine, unsigned regno, u32 value)
{
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->set)(current, rs, pos, count, & value, NULL);
        WARN_ON (rc);
        /* success */
        goto out;
      }
  WARN_ON (1); /* did not find appropriate regset! */
  
 out:
  return;
}


static u64 ursl_fetch64 (const struct usr_regset_lut* lut, unsigned lutsize, int e_machine, unsigned regno)
{
  u64 value = ~0;
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  if (!rsv) goto out;
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->get)(current, rs, pos, count, & value, NULL);
        if (rc)
          goto out;
        /* success */
        return value;
      }
 out:
  printk (KERN_WARNING "process %d mach %d regno %d not available for fetch.\n", current->tgid, e_machine, regno);
  return value;
}


static void ursl_store64 (const struct usr_regset_lut* lut,unsigned lutsize,  int e_machine, unsigned regno, u64 value)
{
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->set)(current, rs, pos, count, & value, NULL);
        if (rc)
          goto out;
        /* success */
        return;
      }
  
 out:
  printk (KERN_WARNING "process %d mach %d regno %d not available for store.\n", current->tgid, e_machine, regno);
  return;
}


#if defined (__i386__)

#define u_fetch_register(regno) ursl_fetch32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno)
#define u_store_register(regno,value) ursl_store32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno, value)

#elif defined (__x86_64__)

#define u_fetch_register(regno) (_stp_probing_32bit_app(c->regs) ? ursl_fetch32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno) : ursl_fetch64(url_x86_64, ARRAY_SIZE(url_x86_64), EM_X86_64, regno))
#define u_store_register(regno,value)  (_stp_probing_32bit_app(c->regs) ? ursl_store32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno, value) : ursl_store64(url_x86_64, ARRAY_SIZE(url_x86_64), EM_X86_64, regno, value))

#else

/* Some other architecture; downgrade to kernel register access. */
#define u_fetch_register(regno) k_fetch_register(regno)
#define u_store_register(regno,value) k_store_register(regno,value)

#endif


#else /* ! STAPCONF_REGSET */
/* Downgrade to kernel register access. */
#define u_fetch_register(regno) k_fetch_register(regno)
#define u_store_register(regno,value) k_store_register(regno,value)
#endif


#if defined (STAPCONF_X86_UNIREGS) && defined (__i386__)

#define k_dwarf_register_0(regs)  regs->ax
#define k_dwarf_register_1(regs)  regs->cx
#define k_dwarf_register_2(regs)  regs->dx
#define k_dwarf_register_3(regs)  regs->bx
#define k_dwarf_register_4(regs)  ((long) &regs->sp)
#define k_dwarf_register_5(regs)  regs->bp
#define k_dwarf_register_6(regs)  regs->si
#define k_dwarf_register_7(regs)  regs->di

#elif defined (STAPCONF_X86_UNIREGS) && defined (__x86_64__)

#define k_dwarf_register_0(regs)  regs->ax
#define k_dwarf_register_1(regs)  regs->dx
#define k_dwarf_register_2(regs)  regs->cx
#define k_dwarf_register_3(regs)  regs->bx
#define k_dwarf_register_4(regs)  regs->si
#define k_dwarf_register_5(regs)  regs->di
#define k_dwarf_register_6(regs)  regs->bp
#define k_dwarf_register_7(regs)  regs->sp
#define k_dwarf_register_8(regs)  regs->r8
#define k_dwarf_register_9(regs)  regs->r9
#define k_dwarf_register_10(regs) regs->r10
#define k_dwarf_register_11(regs) regs->r11
#define k_dwarf_register_12(regs) regs->r12
#define k_dwarf_register_13(regs) regs->r13
#define k_dwarf_register_14(regs) regs->r14
#define k_dwarf_register_15(regs) regs->r15

#elif defined __i386__

/* The stack pointer is unlike other registers.  When a trap happens in
   kernel mode, it is not saved in the trap frame (struct pt_regs).
   The `esp' (and `xss') fields are valid only for a user-mode trap.
   For a kernel mode trap, the interrupted state's esp is actually an
   address inside where the `struct pt_regs' on the kernel trap stack points. */

#define k_dwarf_register_0(regs)	regs->eax
#define k_dwarf_register_1(regs)	regs->ecx
#define k_dwarf_register_2(regs)	regs->edx
#define k_dwarf_register_3(regs)	regs->ebx
#define k_dwarf_register_4(regs)	(user_mode(regs) ? regs->esp : (long)&regs->esp)
#define k_dwarf_register_5(regs)	regs->ebp
#define k_dwarf_register_6(regs)	regs->esi
#define k_dwarf_register_7(regs)	regs->edi

#elif defined __ia64__
#undef k_fetch_register
#undef k_store_register

#define k_fetch_register(regno)		ia64_fetch_register(regno, c->regs, &c->unwaddr)
#define k_store_register(regno,value)	ia64_store_register(regno, c->regs, value)

#elif defined __x86_64__

#define k_dwarf_register_0(regs)	regs->rax
#define k_dwarf_register_1(regs)	regs->rdx
#define k_dwarf_register_2(regs)	regs->rcx
#define k_dwarf_register_3(regs)	regs->rbx
#define k_dwarf_register_4(regs)	regs->rsi
#define k_dwarf_register_5(regs)	regs->rdi
#define k_dwarf_register_6(regs)	regs->rbp
#define k_dwarf_register_7(regs)	regs->rsp
#define k_dwarf_register_8(regs)	regs->r8
#define k_dwarf_register_9(regs)	regs->r9
#define k_dwarf_register_10(regs)	regs->r10
#define k_dwarf_register_11(regs)	regs->r11
#define k_dwarf_register_12(regs)	regs->r12
#define k_dwarf_register_13(regs)	regs->r13
#define k_dwarf_register_14(regs)	regs->r14
#define k_dwarf_register_15(regs)	regs->r15

#elif defined __powerpc__

#undef k_fetch_register
#undef k_store_register
#define k_fetch_register(regno) ((intptr_t) c->regs->gpr[regno])
#define k_store_register(regno,value) (c->regs->gpr[regno] = (value))

#elif defined (__arm__)

#undef k_fetch_register
#undef k_store_register
#define k_fetch_register(regno) ((long) c->regs->uregs[regno])
#define k_store_register(regno,value) (c->regs->uregs[regno] = (value))

#elif defined (__s390__) || defined (__s390x__)
#undef k_fetch_register
#undef k_store_register
#define k_fetch_register(regno) ((intptr_t) c->regs->gprs[regno])
#define k_store_register(regno,value) (c->regs->gprs[regno] = (value))

#endif


/* NB: this autoconf is always disabled, pending further performance eval. */
#if 0 && defined STAPCONF_PROBE_KERNEL

/* Kernel 2.6.26 adds probe_kernel_{read,write}, which lets us write
 * architecture-neutral implementations of kread, kwrite, deref, and
 * store_deref.
 *
 * NB: deref and store_deref shouldn't be used with 64-bit values on 32-bit
 * platforms, because they will lose data in the conversion to intptr_t.  We
 * generally want to encourage using kread and kwrite instead.
 */

#define kread(ptr) ({ \
        typeof(*(ptr)) _v = 0; \
        if (lookup_bad_addr((unsigned long)(ptr), sizeof (*(ptr))) ||	\
            probe_kernel_read((void *)&_v, (void *)(ptr), sizeof(*(ptr)))) \
          DEREF_FAULT(ptr); \
        _v; \
    })

#define kwrite(ptr, value) ({ \
        typeof(*(ptr)) _v; \
        _v = (typeof(*(ptr)))(value); \
        if (lookup_bad_addr((unsigned long)addr, sizeof (*(ptr))) ||	\
            probe_kernel_write((void *)(ptr), (void *)&_v, sizeof(*(ptr)))) \
          STORE_DEREF_FAULT(ptr); \
    })

#define deref(size, addr) ({ \
    intptr_t _i = 0; \
    switch (size) { \
      case 1: _i = kread((u8 *)(addr)); break; \
      case 2: _i = kread((u16 *)(addr)); break; \
      case 4: _i = kread((u32 *)(addr)); break; \
      case 8: _i = kread((u64 *)(addr)); break; \
      default: __deref_bad(); \
    } \
    _i; \
  })

#define store_deref(size, addr, value) ({ \
    switch (size) { \
      case 1: kwrite((u8 *)(addr), (value)); break; \
      case 2: kwrite((u16 *)(addr), (value)); break; \
      case 4: kwrite((u32 *)(addr), (value)); break; \
      case 8: kwrite((u64 *)(addr), (value)); break; \
      default: __store_deref_bad(); \
    } \
  })


extern void __deref_bad(void);
extern void __store_deref_bad(void);

#else /* !STAPCONF_PROBE_KERNEL */

#if defined __i386__

#define deref(size, addr)						      \
  ({									      \
    int _bad = 0;							      \
    u8 _b; u16 _w; u32 _l;	                                              \
    intptr_t _v = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __get_user_asm(_b,addr,_bad,"b","b","=q",1); _v = _b; break;  \
        case 2: __get_user_asm(_w,addr,_bad,"w","w","=r",1); _v = _w; break;  \
        case 4: __get_user_asm(_l,addr,_bad,"l","","=r",1); _v = _l; break;   \
        default: _v = __get_user_bad();                                       \
        }                                                                     \
    if (_bad)                                                                 \
      DEREF_FAULT(addr);						      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __put_user_asm(((u8)(value)),addr,_bad,"b","b","iq",1); break;\
        case 2: __put_user_asm(((u16)(value)),addr,_bad,"w","w","ir",1); break;\
        case 4: __put_user_asm(((u32)(value)),addr,_bad,"l","k","ir",1); break;\
        default: __put_user_bad();                                            \
        }                                                                     \
    if (_bad)                                                                 \
      STORE_DEREF_FAULT(addr);                                                \
  })


#elif defined __x86_64__

#define deref(size, addr)						      \
  ({									      \
    int _bad = 0;							      \
    u8 _b; u16 _w; u32 _l; u64 _q;					      \
    intptr_t _v = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __get_user_asm(_b,(unsigned long)addr,_bad,"b","b","=q",1); _v = _b; break;  \
        case 2: __get_user_asm(_w,(unsigned long)addr,_bad,"w","w","=r",1); _v = _w; break;  \
        case 4: __get_user_asm(_l,(unsigned long)addr,_bad,"l","","=r",1); _v = _l; break;   \
        case 8: __get_user_asm(_q,(unsigned long)addr,_bad,"q","","=r",1); _v = _q; break;   \
        default: _v = __get_user_bad();                                       \
        }                                                                     \
    if (_bad)								      \
      DEREF_FAULT(addr);							      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __put_user_asm(((u8)(value)),addr,_bad,"b","b","iq",1); break; \
        case 2: __put_user_asm(((u16)(value)),addr,_bad,"w","w","ir",1); break;\
        case 4: __put_user_asm(((u32)(value)),addr,_bad,"l","k","ir",1); break;\
        case 8: __put_user_asm(((u64)(value)),addr,_bad,"q","","Zr",1); break; \
        default: __put_user_bad();                                      \
        }                                                               \
    if (_bad)								      \
      STORE_DEREF_FAULT(addr);							      \
  })

#elif defined __ia64__
#define deref(size, addr)						\
  ({									\
     int _bad = 0;							\
     intptr_t _v=0;							\
     if (lookup_bad_addr((unsigned long)addr, size))			\
       _bad = 1;                                                        \
     else                                                               \
       switch (size){							\
       case 1: __get_user_size(_v, addr, 1, _bad); break; 		\
       case 2: __get_user_size(_v, addr, 2, _bad); break;  		\
       case 4: __get_user_size(_v, addr, 4, _bad); break;  		\
       case 8: __get_user_size(_v, addr, 8, _bad); break;  		\
       default: __get_user_unknown(); break;				\
       }								\
    if (_bad)  								\
	DEREF_FAULT(addr);						\
     _v;								\
   })

#define store_deref(size, addr, value)					\
  ({									\
    int _bad=0;								\
    if (lookup_bad_addr((unsigned long)addr, size))			\
      _bad = 1;                                                         \
    else                                                                \
      switch (size){							\
      case 1: __put_user_size(value, addr, 1, _bad); break;		\
      case 2: __put_user_size(value, addr, 2, _bad); break;		\
      case 4: __put_user_size(value, addr, 4, _bad); break;		\
      case 8: __put_user_size(value, addr, 8, _bad); break;		\
      default: __put_user_unknown(); break;				\
      }                                                                 \
    if (_bad)								\
	   STORE_DEREF_FAULT(addr);						\
   })

#elif defined __powerpc__ || defined __powerpc64__
#if defined __powerpc64__
#define STP_PPC_LONG	".llong "
#else
#define STP_PPC_LONG	".long "
#endif

#define __stp_get_user_asm(x, addr, err, op)			\
	 __asm__ __volatile__(					\
		"1:     "op" %1,0(%2)   # get_user\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:     li %0,%3\n"				\
		"       li %1,0\n"				\
		"       b 2b\n"					\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"       .balign %5\n"				\
		STP_PPC_LONG "1b,3b\n"				\
		".previous"					\
		: "=r" (err), "=r" (x)				\
		: "b" (addr), "i" (-EFAULT), "0" (err),		\
		  "i"(sizeof(unsigned long)))


#define __stp_put_user_asm(x, addr, err, op)                        \
        __asm__ __volatile__(                                   \
                "1:     " op " %1,0(%2) # put_user\n"           \
                "2:\n"                                          \
                ".section .fixup,\"ax\"\n"                      \
                "3:     li %0,%3\n"                             \
                "       b 2b\n"                                 \
                ".previous\n"                                   \
                ".section __ex_table,\"a\"\n"                   \
                "       .balign %5\n"				\
                STP_PPC_LONG "1b,3b\n"				\
                ".previous"                                     \
                : "=r" (err)                                    \
                : "r" (x), "b" (addr), "i" (-EFAULT), "0" (err),\
		  "i"(sizeof(unsigned long)))


#define deref(size, addr)						      \
  ({									      \
    int _bad = 0;							      \
    intptr_t _v = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __stp_get_user_asm(_v,addr,_bad,"lbz"); break;                \
        case 2: __stp_get_user_asm(_v,addr,_bad,"lhz"); break;                \
        case 4: __stp_get_user_asm(_v,addr,_bad,"lwz"); break;                \
        case 8: __stp_get_user_asm(_v,addr,_bad,"ld"); break;                 \
        default: _v = __get_user_bad();                                       \
        }                                                                     \
    if (_bad)								      \
      DEREF_FAULT(addr);						      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    if (lookup_bad_addr((unsigned long)addr, size))			      \
      _bad = 1;                                                               \
    else                                                                      \
      switch (size)                                                           \
        {                                                                     \
        case 1: __stp_put_user_asm(((u8)(value)),addr,_bad,"stb"); break;     \
        case 2: __stp_put_user_asm(((u16)(value)),addr,_bad,"sth"); break;    \
        case 4: __stp_put_user_asm(((u32)(value)),addr,_bad,"stw"); break;    \
        case 8: __stp_put_user_asm(((u64)(value)),addr,_bad, "std"); break;   \
        default: __put_user_bad();                                            \
        }                                                                     \
    if (_bad)								      \
      STORE_DEREF_FAULT(addr);							      \
  })

#elif defined (__arm__)

/* Macros for ARM lifted from 2.6.21.1's linux/include/asm-arm/uaccess.h
 * and slightly altered. */

#define __stp_get_user_asm_byte(x,addr,err)			\
	__asm__ __volatile__(					\
	"1:	ldrb	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	mov	%1, #0\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err), "=&r" (x)					\
	: "r" (addr), "i" (-EFAULT)				\
	: "cc")

#ifndef __ARMEB__
#define __stp_get_user_asm_half(x,__gu_addr,err)		\
({								\
	unsigned long __b1, __b2;				\
	__stp_get_user_asm_byte(__b1, __gu_addr, err);		\
	__stp_get_user_asm_byte(__b2, __gu_addr + 1, err);	\
	(x) = __b1 | (__b2 << 8);				\
})
#else
#define __stp_get_user_asm_half(x,__gu_addr,err)		\
({								\
	unsigned long __b1, __b2;				\
	__stp_get_user_asm_byte(__b1, __gu_addr, err);		\
	__stp_get_user_asm_byte(__b2, __gu_addr + 1, err);	\
	(x) = (__b1 << 8) | __b2;				\
})
#endif

#define __stp_get_user_asm_word(x,addr,err)			\
	__asm__ __volatile__(					\
	"1:	ldr	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	mov	%1, #0\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err), "=&r" (x)					\
	: "r" (addr), "i" (-EFAULT)				\
	: "cc")

#define __stp_put_user_asm_byte(x,__pu_addr,err)		\
	__asm__ __volatile__(					\
	"1:	strb	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err)						\
	: "r" (x), "r" (__pu_addr), "i" (-EFAULT)		\
	: "cc")

#ifndef __ARMEB__
#define __stp_put_user_asm_half(x,__pu_addr,err)			\
({									\
	unsigned long __temp = (unsigned long)(x);			\
	__stp_put_user_asm_byte(__temp, __pu_addr, err);		\
	__stp_put_user_asm_byte(__temp >> 8, __pu_addr + 1, err);	\
})
#else
#define __stp_put_user_asm_half(x,__pu_addr,err)			\
({									\
	unsigned long __temp = (unsigned long)(x);			\
	__stp_put_user_asm_byte(__temp >> 8, __pu_addr, err);		\
	__stp_put_user_asm_byte(__temp, __pu_addr + 1, err);		\
})
#endif

#define __stp_put_user_asm_word(x,__pu_addr,err)		\
	__asm__ __volatile__(					\
	"1:	str	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err)						\
	: "r" (x), "r" (__pu_addr), "i" (-EFAULT)		\
	: "cc")

#ifndef __ARMEB__
#define	__reg_oper0	"%R2"
#define	__reg_oper1	"%Q2"
#else
#define	__reg_oper0	"%Q2"
#define	__reg_oper1	"%R2"
#endif

#define __stp_put_user_asm_dword(x,__pu_addr,err)		\
	__asm__ __volatile__(					\
	"1:	str	" __reg_oper1 ", [%1], #4\n"		\
	"2:	str	" __reg_oper0 ", [%1], #0\n"		\
	"3:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"4:	mov	%0, %3\n"				\
	"	b	3b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 4b\n"				\
	"	.long	2b, 4b\n"				\
	"	.previous"					\
	: "+r" (err), "+r" (__pu_addr)				\
	: "r" (x), "i" (-EFAULT)				\
	: "cc")

#define deref(size, addr)						\
  ({									\
     int _bad = 0;							\
     intptr_t _v=0;							\
     if (lookup_bad_addr((unsigned long)addr, size))			\
      _bad = 1;                                                         \
    else                                                                \
      switch (size){							\
      case 1: __stp_get_user_asm_byte(_v, addr, _bad); break;           \
      case 2: __stp_get_user_asm_half(_v, addr, _bad); break;           \
      case 4: __stp_get_user_asm_word(_v, addr, _bad); break;           \
      default: __get_user_bad(); break;                                 \
      }                                                                 \
    if (_bad)  								\
	DEREF_FAULT(addr);						\
     _v;								\
   })

#define store_deref(size, addr, value)					\
  ({									\
    int _bad=0;								\
    if (lookup_bad_addr((unsigned long)addr, size))			\
      _bad = 1;                                                         \
    else                                                                \
      switch (size){							\
      case 1: __stp_put_user_asm_byte(value, addr, _bad); break;	\
      case 2: __stp_put_user_asm_half(value, addr, _bad); break;	\
      case 4: __stp_put_user_asm_word(value, addr, _bad); break;	\
      case 8: __stp_put_user_asm_dword(value, addr, _bad); break;	\
      default: __put_user_bad(); break;                                 \
      }                                                                 \
    if (_bad)								\
	   STORE_DEREF_FAULT(addr);						\
   })

#elif defined (__s390__) || defined (__s390x__)

#ifndef EX_TABLE
/*
 * Helper macro for exception table entries
 */
#ifndef __s390x__
#define EX_TABLE(_fault,_target)				\
		".section __ex_table,\"a\"\n"			\
		"       .align 4\n"				\
		"       .long  " #_fault "," #_target "\n"	\
		".previous\n"
#else
#define EX_TABLE(_fault,_target)				\
		".section __ex_table,\"a\"\n"			\
		"       .align 8\n"				\
		"       .quad  " #_fault "," #_target "\n"	\
		".previous\n"
#endif
#endif

#define __stp_get_asm(x, addr, err, size)			\
({								\
	asm volatile(						\
		"0: mvc  0(%2,%4),0(%3)\n"			\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lghi    %0,%5\n"				\
		"   jg     1b\n"				\
		".previous\n"					\
		EX_TABLE(0b,2b)					\
		: "+&d" (err), "=m" (x)				\
		: "i" (size),"a"(addr),				\
		"a" (&(x)),"K" (-EFAULT)			\
		: "cc");					\
})

#define __stp_put_asm(x, addr, err)				\
({								\
	asm volatile(						\
		"0: stc %2,0(%1)\n"				\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lghi    %0,%3\n"				\
		"   jg     1b\n"				\
		".previous\n"					\
		EX_TABLE(0b,2b)					\
		: "+&d" (err)					\
		: "a"(addr),					\
		"r"(x),"K"(-EFAULT)				\
		: "cc");					\
})

#define deref(size, addr)					\
({								\
	u8 _b; u16 _w; u32 _l; u64 _q;				\
	int _bad = 0;						\
	intptr_t _v = 0;					\
        if (lookup_bad_addr((unsigned long)addr, size))		\
          _bad = 1;                                             \
        else                                                    \
          switch (size) {		                	\
          case 1: {                                             \
		__stp_get_asm(_b, addr, _bad, 1);		\
		_v = _b;					\
		break;						\
          };                                                    \
          case 2: {						\
		__stp_get_asm(_w, addr, _bad, 2);		\
		_v = _w;					\
		break;						\
          };                                                    \
          case 4: {						\
		__stp_get_asm(_l, addr, _bad, 4);		\
		_v = _l;					\
		break;						\
          };                                                    \
          case 8: {						\
		__stp_get_asm(_q, addr, _bad, 8);		\
		_v = _q;					\
		break;						\
          };                                                    \
          default:						\
		_bad = -EFAULT;					\
	}							\
	if (_bad)						\
		DEREF_FAULT(addr);				\
	_v;							\
})

#define store_deref(size, addr, value)                          \
({                                                              \
        int _bad = 0;                                           \
	int i;							\
        if (lookup_bad_addr((unsigned long)addr, size))		\
          _bad = 1;                                             \
        else                                                    \
          for(i=0;i<size;i++){                                  \
            __stp_put_asm((u8)(value>>((size-i-1)*8)&0xff),     \
                          (u64)addr+i,_bad);                    \
            if (_bad)                                           \
              break;                                            \
          }                                                     \
        if (_bad)                                               \
          STORE_DEREF_FAULT(addr);                              \
})


#endif /* (s390) || (s390x) */


#if defined __i386__

/* x86 can't do 8-byte put/get_user_asm, so we have to split it */

#define kread(ptr)					\
  ((sizeof(*(ptr)) == 8) ?				\
       *(typeof(ptr))&(u32[2]) {			\
	 (u32) deref(4, &((u32 *)(ptr))[0]),		\
	 (u32) deref(4, &((u32 *)(ptr))[1]) }		\
     : (typeof(*(ptr))) deref(sizeof(*(ptr)), (ptr)))

#define kwrite(ptr, value)						     \
  ({									     \
    if (sizeof(*(ptr)) == 8) {						     \
      union { typeof(*(ptr)) v; u32 l[2]; } _kw;			     \
      _kw.v = (typeof(*(ptr)))(value);					     \
      store_deref(4, &((u32 *)(ptr))[0], _kw.l[0]);			     \
      store_deref(4, &((u32 *)(ptr))[1], _kw.l[1]);			     \
    } else								     \
      store_deref(sizeof(*(ptr)), (ptr), (long)(typeof(*(ptr)))(value));     \
  })

#else

#define kread(ptr) \
  ( (typeof(*(ptr))) deref(sizeof(*(ptr)), (ptr)) )
#define kwrite(ptr, value) \
  ( store_deref(sizeof(*(ptr)), (ptr), (long)(typeof(*(ptr)))(value)) )

#endif

#endif /* STAPCONF_PROBE_KERNEL */


#define deref_string(dst, addr, maxbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    unsigned char _c;							      \
    char *_d = (dst);							      \
    for (_len = (maxbytes), _addr = (uintptr_t)(addr);			      \
	 _len > 1 && (_c = deref (1, _addr)) != '\0';			      \
	 --_len, ++_addr)						      \
      if (_d)								      \
	 *_d++ = _c;							      \
    if (_d)								      \
      *_d = '\0';							      \
    (dst);								      \
  })

#define deref_buffer(dst, addr, numbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    unsigned char _c;							      \
    char *_d = (dst);							      \
    for (_len = (numbytes), _addr = (uintptr_t)(addr);			      \
	 _len >= 1;			                                      \
	 --_len, ++_addr) {						      \
      _c = deref (1, _addr);						      \
      if (_d)								      \
	 *_d++ = _c;							      \
    }                                                                         \
    (dst);								      \
  })

#define store_deref_string(src, addr, maxbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    char *_s = (src);							      \
    for (_len = (maxbytes), _addr = (uintptr_t)(addr);			      \
	 _len > 1 && _s && *_s != '\0'; --_len, ++_addr)		      \
      store_deref(1, _addr, *_s++);					      \
    store_deref(1, _addr, '\0');					      \
  })

#define CATCH_DEREF_FAULT()				\
  if (0) {						\
deref_fault: ;						\
  }
