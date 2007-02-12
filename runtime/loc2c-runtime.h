/* target operations
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2005, 2006, 2007 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

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
  target = (target							      \
	    &~ ((((__typeof (base)) 1 << (nbits)) - 1)			      \
		<< (sizeof (base) * 8 - (higherbits) - (nbits)))	      \
	    | ((__typeof (base)) (base)					      \
	       << (sizeof (base) * 8 - (higherbits) - (nbits))))


/* These operations are target-specific.  */
#include <asm/uaccess.h>

/* Given a DWARF register number, fetch its intptr_t (long) value from the
   probe context, or store a new value into the probe context.

   The register number argument is always a canonical decimal number, so it
   can be pasted into an identifier name.  These definitions turn it into a
   per-register macro, defined below for machines with individually-named
   registers.  */
#define fetch_register(regno) \
  ((intptr_t) dwarf_register_##regno (c->regs))
#define store_register(regno, value) \
  (dwarf_register_##regno (c->regs) = (value))


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

#if defined __i386__

/* The stack pointer is unlike other registers.  When a trap happens in
   kernel mode, it is not saved in the trap frame (struct pt_regs).
   The `esp' (and `xss') fields are valid only for a user-mode trap.
   For a kernel mode trap, the interrupted state's esp is actually an
   address inside where the `struct pt_regs' on the kernel trap stack points.

   For now we assume all traps are from kprobes in kernel-mode code.
   For extra paranoia, could do BUG_ON((regs->xcs & 3) == 3).  */

#define dwarf_register_0(regs)	regs->eax
#define dwarf_register_1(regs)	regs->ecx
#define dwarf_register_2(regs)	regs->edx
#define dwarf_register_3(regs)	regs->ebx
#define dwarf_register_4(regs)	((long) &regs->esp)
#define dwarf_register_5(regs)	regs->ebp
#define dwarf_register_6(regs)	regs->esi
#define dwarf_register_7(regs)	regs->edi

#elif defined __ia64__
#undef fetch_register
#undef store_register

#define fetch_register(regno)		ia64_fetch_register(regno, c->regs)
#define store_register(regno, value)	ia64_store_register(regno, c->regs, value)

#elif defined __x86_64__

#define dwarf_register_0(regs)	regs->rax
#define dwarf_register_1(regs)	regs->rdx
#define dwarf_register_2(regs)	regs->rcx
#define dwarf_register_3(regs)	regs->rbx
#define dwarf_register_4(regs)	regs->rsi
#define dwarf_register_5(regs)	regs->rdi
#define dwarf_register_6(regs)	regs->rbp
#define dwarf_register_7(regs)	regs->rsp
#define dwarf_register_8(regs)	regs->r8
#define dwarf_register_9(regs)	regs->r9
#define dwarf_register_10(regs)	regs->r10
#define dwarf_register_11(regs)	regs->r11
#define dwarf_register_12(regs)	regs->r12
#define dwarf_register_13(regs)	regs->r13
#define dwarf_register_14(regs)	regs->r14
#define dwarf_register_15(regs)	regs->r15

#elif defined __powerpc__

#undef fetch_register
#undef store_register
#define fetch_register(regno) ((intptr_t) c->regs->gpr[regno])
#define store_register(regno) (c->regs->gpr[regno] = (value))

#elif defined (__s390__) || defined (__s390x__)
#undef fetch_register
#undef store_register
#define fetch_register(regno) ((intptr_t) c->regs->gprs[regno])
#define store_register(regno) (c->regs->gprs[regno] = (value))

#endif

#if defined __i386__

#define deref(size, addr)						      \
  ({									      \
    int _bad = 0;							      \
    u8 _b; u16 _w; u32 _l;	                                              \
    intptr_t _v;							      \
    switch (size)							      \
      {									      \
      case 1: __get_user_asm(_b,addr,_bad,"b","b","=q",1); _v = _b; break;    \
      case 2: __get_user_asm(_w,addr,_bad,"w","w","=r",1); _v = _w; break;    \
      case 4: __get_user_asm(_l,addr,_bad,"l","","=r",1); _v = _l; break;     \
      default: _v = __get_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    switch (size)							      \
      {									      \
      case 1: __put_user_asm(((u8)(value)),addr,_bad,"b","b","iq",1); break;  \
      case 2: __put_user_asm(((u16)(value)),addr,_bad,"w","w","ir",1); break; \
      case 4: __put_user_asm(((u32)(value)),addr,_bad,"l","k","ir",1); break; \
      default: __put_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
  })


#elif defined __x86_64__

#define deref(size, addr)						      \
  ({									      \
    int _bad = 0;							      \
    u8 _b; u16 _w; u32 _l; u64 _q;					      \
    intptr_t _v;							      \
    switch (size)							      \
      {									      \
      case 1: __get_user_asm(_b,addr,_bad,"b","b","=q",1); _v = _b; break;    \
      case 2: __get_user_asm(_w,addr,_bad,"w","w","=r",1); _v = _w; break;    \
      case 4: __get_user_asm(_l,addr,_bad,"l","","=r",1); _v = _l; break;     \
      case 8: __get_user_asm(_q,addr,_bad,"q","","=r",1); _v = _q; break;     \
      default: _v = __get_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    switch (size)							      \
      {									      \
      case 1: __put_user_asm(((u8)(value)),addr,_bad,"b","b","iq",1); break;  \
      case 2: __put_user_asm(((u16)(value)),addr,_bad,"w","w","ir",1); break; \
      case 4: __put_user_asm(((u32)(value)),addr,_bad,"l","k","ir",1); break; \
      case 8: __put_user_asm(((u64)(value)),addr,_bad,"q","","Zr",1); break;  \
      default: __put_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
  })

#elif defined __ia64__
#define deref(size, addr)						\
  ({									\
     int _bad = 0;							\
     intptr_t _v=0;							\
	switch (size){							\
	case 1: __get_user_size(_v, addr, 1, _bad); break; 		\
	case 2: __get_user_size(_v, addr, 2, _bad); break;  		\
	case 4: __get_user_size(_v, addr, 4, _bad); break;  		\
	case 8: __get_user_size(_v, addr, 8, _bad); break;  		\
	default: __get_user_unknown(); break;				\
	}								\
    if (_bad)  								\
	goto deref_fault;						\
     _v;								\
   })

#define store_deref(size, addr, value)					\
  ({									\
    int _bad=0;								\
	switch (size){							\
	case 1: __put_user_size(value, addr, 1, _bad); break;		\
	case 2: __put_user_size(value, addr, 2, _bad); break;		\
	case 4: __put_user_size(value, addr, 4, _bad); break;		\
	case 8: __put_user_size(value, addr, 8, _bad); break;		\
	default: __put_user_unknown(); break;				\
	}								\
    if (_bad)								\
	   goto deref_fault;						\
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
    intptr_t _v;							      \
    switch (size)							      \
      {									      \
      case 1: __stp_get_user_asm(_v,addr,_bad,"lbz"); break;		      \
      case 2: __stp_get_user_asm(_v,addr,_bad,"lhz"); break;		      \
      case 4: __stp_get_user_asm(_v,addr,_bad,"lwz"); break;		      \
      case 8: __stp_get_user_asm(_v,addr,_bad,"ld"); break;			      \
      default: _v = __get_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
    _v;									      \
  })

#define store_deref(size, addr, value)					      \
  ({									      \
    int _bad = 0;							      \
    switch (size)							      \
      {									      \
      case 1: __stp_put_user_asm(((u8)(value)),addr,_bad,"stb"); break;     \
      case 2: __stp_put_user_asm(((u16)(value)),addr,_bad,"sth"); break;    \
      case 4: __stp_put_user_asm(((u32)(value)),addr,_bad,"stw"); break;    \
      case 8: __stp_put_user_asm(((u64)(value)),addr,_bad, "std"); break;         \
      default: __put_user_bad();					      \
      }									      \
    if (_bad)								      \
      goto deref_fault;							      \
  })

#elif defined (__s390__) || defined (__s390x__)

#if defined __s390__
#define __stp_get_asm(x, addr, err, size)			\
({								\
	asm volatile(						\
		"0: mvc  0(%2,%4),0(%3)\n"			\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lhi    %0,%5\n"				\
		"   bras    1,3f\n"				\
		"   .long  1b\n"				\
		"3: l      1,0(1)\n"				\
		"   br     1\n"					\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"   .align 4\n"					\
		"   .long  0b,2b\n"				\
		".previous"					\
		: "+&d" (err), "=m" (x)				\
		: "i" (size),"a"(addr),				\
		"a" (&(x)),"K" (-EFAULT)			\
		: "cc", "1" );					\
})

#define __stp_put_asm(x, addr, err, size)			\
({								\
	asm volatile(						\
		"0: mvc  0(%1,%2),0(%3)\n"			\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lhi    %0,%5\n"				\
		"   bras    1,3f\n"				\
		"   .long  1b\n"				\
		"3: l      1,0(1)\n"				\
		"   br     1\n"					\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"   .align 4\n"					\
		"   .long  0b,2b\n"				\
		".previous"					\
		: "+&d" (err)					\
		: "i" (size), "a" (addr),			\
		"a" (&(x)),"K" (-EFAULT)			\
		: "cc", "1");					\
})

#else /* s390x */

#define __stp_get_asm(x, addr, err, size)			\
({								\
	asm volatile(						\
		"0: mvc  0(%2,%4),0(%3)\n"			\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lghi    %0,%5\n"				\
		"   jg     1b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"   .align 8\n"					\
		"   .quad  0b,2b\n"				\
		".previous"					\
		: "+&d" (err), "=m" (x)				\
		: "i" (size),"a"(addr),				\
		"a" (&(x)),"K" (-EFAULT)			\
		: "cc");					\
})

#define __stp_put_asm(x, addr, err, size)			\
({								\
	asm volatile(						\
		"0: mvc  0(%1,%2),0(%3)\n"			\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		"2: lghi    %0,%4\n"				\
		"   jg     1b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"   .align 8\n"					\
		"   .quad  0b,2b\n"				\
		".previous"					\
		: "+&d" (err)					\
		: "i" (size),"a"(addr),				\
		"a"(&(x)),"K"(-EFAULT)				\
		: "cc");					\
})
#endif

#define deref(size, addr)					\
({								\
	u8 _b; u16 _w; u32 _l; u64 _q;				\
	int _bad = 0;						\
	intptr_t _v = 0;					\
	switch (size) {						\
	case 1: {						\
		__stp_get_asm(_b, addr, _bad, 1);		\
		_v = _b;					\
		break;						\
		};						\
	case 2: {						\
		__stp_get_asm(_w, addr, _bad, 2);		\
		_v = _w;					\
		break;						\
		};						\
	case 4: {						\
		__stp_get_asm(_l, addr, _bad, 4);		\
		_v = _l;					\
		break;						\
		};						\
	case 8: {						\
		__stp_get_asm(_q, addr, _bad, 8);		\
		_v = _q;					\
		break;						\
		};						\
	default:						\
		_bad = -EFAULT;					\
	}							\
	if (_bad)						\
		goto deref_fault;				\
	_v;							\
})

#define store_deref(size, addr, value)				\
({								\
	int _bad = 0;						\
	switch (size) {						\
	case 1:{						\
		u8 _x = value;					\
		__stp_put_asm(_x, addr, _bad,1);		\
		break;						\
		};						\
	case 2:{ 						\
		u16 _x = value;					\
		__stp_put_asm(_x, addr, _bad,2);		\
		break;						\
		};						\
	case 4:{						\
		u32 _x = value;					\
		__stp_put_asm(_x, addr, _bad,4);		\
		break;						\
		};						\
	case 8:	{						\
		u64 _x = value;					\
		__stp_put_asm(_x, addr, _bad,8);		\
		break;						\
		};						\
	default:						\
		break;						\
	}							\
	if (_bad)						\
		goto deref_fault;				\
})
#endif /* (s390) || (s390x) */

#define deref_string(dst, addr, maxbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    unsigned char _c;							      \
    char *_d = (dst);							      \
    for (_len = (maxbytes), _addr = (uintptr_t)(addr);			      \
	 _len > 1 && (_c = deref (1, _addr)) != '\0';			      \
	 --_len, ++_addr)						      \
      *_d++ = _c;							      \
    *_d = '\0';								      \
    (dst);								      \
  })


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

#define CATCH_DEREF_FAULT()				\
  if (0) {						\
deref_fault:						\
    CONTEXT->last_error = "pointer dereference fault";	\
  }
