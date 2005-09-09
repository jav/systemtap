/* -*- linux-c -*- */
/* Math functions
 * Copyright (C) 2005 Red Hat Inc.
 * Portions (C)  Free Software Foundation, Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ARITH_C_ 
#define _ARITH_C_

/** @file arith.
 * @brief Implements various arithmetic-related helper functions
 */


/* 64-bit division for 64-bit cpus and i386 */
/* Other 32-bit cpus will need to modify this file. */

#ifdef __i386__
long long _div64 (long long u, long long v);
long long _mod64 (long long u, long long v);
#endif

/** Divide x by y.  In case of overflow or division-by-zero,
 * set context error string, and return any old value.
 */
int64_t _stp_div64 (const char **error, int64_t x, int64_t y)
{
#ifdef __LP64__
	if (unlikely (y == 0 || (x == LONG_MIN && y == -1))) {	
		if (error) *error = "divisor out of range";
		return 0;
	}
	return x/y;
#else
	if (likely ((x > LONG_MIN && x < LONG_MAX) && (y > LONG_MIN && y < LONG_MAX))) {
		long xx = (long) x;
		long yy = (long) y;

		// check for division-by-zero
		if (unlikely (yy == 0 )) {
			if (error) *error = "division by 0";
			return 0;
		}
		return xx / yy;
	} else
		return _div64 (x, y);
#endif
}


/** Modulo x by y.  In case of overflow or division-by-zero,
 * set context error string, and return any old value.
 */
int64_t _stp_mod64 (const char **error, int64_t x, int64_t y)
{
#ifdef __LP64__
	if (unlikely (y == 0 || (x == LONG_MIN && y == -1))) {	
		if (error) *error = "divisor out of range";
		return 0;
	}
	return x%y;

#else

	if (likely ((x > LONG_MIN && x < LONG_MAX) && (y > LONG_MIN && y < LONG_MAX))) {
		long xx = (long) x;
		long yy = (long) y;

		// check for division-by-zero
		if (unlikely (yy == 0)) {
			if (error) *error = "division by 0";
			return 0;
		}
		return xx % yy;
	} else
		return _mod64 (x,y);
#endif
}


/** Return a random integer between -n and n.
 * @param n how far from zero to go.  Make it positive but less than a million or so.
 */
int _stp_random_pm (int n)
{
	static unsigned long seed;
	static int initialized_p = 0;
	
	if (unlikely (! initialized_p)) {
		seed = (unsigned long) jiffies;
		initialized_p = 1;
	}
	
	/* from glibc rand man page */
	seed = seed * 1103515245 + 12345;
	
	return (seed % (2*n+1)-n);
}

#ifdef __i386__

/* 64-bit division functions extracted from libgcc */
typedef long long DWtype;
typedef unsigned long long UDWtype;
typedef unsigned long UWtype;
typedef long Wtype;
typedef unsigned int USItype;

#ifdef _BIG_ENDIAN
struct DWstruct {Wtype high, low;};
#else
struct DWstruct {Wtype low, high;};
#endif

#define W_TYPE_SIZE 32

typedef union
{
  struct DWstruct s;
  DWtype ll;
} DWunion;


/* these are the i386 versions of these macros from gcc/longlong.h */

#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("mull %3"							\
	   : "=a" ((USItype) (w0)),					\
	     "=d" ((USItype) (w1))					\
	   : "%0" ((USItype) (u)),					\
	     "rm" ((USItype) (v)))

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  __asm__ ("subl %5,%1\n\tsbbl %3,%0"					\
	   : "=r" ((USItype) (sh)),					\
	     "=&r" ((USItype) (sl))					\
	   : "0" ((USItype) (ah)),					\
	     "g" ((USItype) (bh)),					\
	     "1" ((USItype) (al)),					\
	     "g" ((USItype) (bl)))

#define udiv_qrnnd(q, r, n1, n0, dv) \
  __asm__ ("divl %4"							\
	   : "=a" ((USItype) (q)),					\
	     "=d" ((USItype) (r))					\
	   : "0" ((USItype) (n0)),					\
	     "1" ((USItype) (n1)),					\
	     "rm" ((USItype) (dv)))

#define count_leading_zeros(count, x) \
  do {									\
    USItype __cbtmp;							\
    __asm__ ("bsrl %1,%0"						\
	     : "=r" (__cbtmp) : "rm" ((USItype) (x)));			\
    (count) = __cbtmp ^ 31;						\
  } while (0)

inline UDWtype _stp_udivmoddi4 (UDWtype n, UDWtype d, UDWtype *rp)
{
	const DWunion nn = {.ll = n};
	const DWunion dd = {.ll = d};
	DWunion ww,rr;
	UWtype d0, d1, n0, n1, n2;
	UWtype q0, q1;
	UWtype b, bm;
	
	d0 = dd.s.low;
	d1 = dd.s.high;
	n0 = nn.s.low;
	n1 = nn.s.high;
	
	if (d1 == 0) {
		if (d0 > n1)	{
			/* 0q = nn / 0D */
			udiv_qrnnd (q0, n0, n1, n0, d0);
			q1 = 0;
			/* Remainder in n0.  */
		} else {
			/* qq = NN / 0d */
			if (d0 == 0)
				d0 = 1 / d0;	/* Divide intentionally by zero.  */
			udiv_qrnnd (q1, n1, 0, n1, d0);
			udiv_qrnnd (q0, n0, n1, n0, d0);
			/* Remainder in n0.  */
		}
		
		if (rp != 0)	{
			rr.s.low = n0;
			rr.s.high = 0;
			*rp = rr.ll;
		}
	} else {
		if (d1 > n1) {
			/* 00 = nn / DD */
			q0 = 0;
			q1 = 0;
			
			/* Remainder in n1n0.  */
			if (rp != 0) {
				rr.s.low = n0;
				rr.s.high = n1;
				*rp = rr.ll;
			}
		} else {
			/* 0q = NN / dd */
			count_leading_zeros (bm, d1);
			if (bm == 0) {
				/* From (n1 >= d1) /\ (the most significant bit of d1 is set),
				   conclude (the most significant bit of n1 is set) /\ (the
				   quotient digit q0 = 0 or 1).
				   This special case is necessary, not an optimization.  */
				
				/* The condition on the next line takes advantage of that
				   n1 >= d1 (true due to program flow).  */
				if (n1 > d1 || n0 >= d0) {
					q0 = 1;
					sub_ddmmss (n1, n0, n1, n0, d1, d0);
				} else
					q0 = 0;
				
				q1 = 0;
				
				if (rp != 0) {
					rr.s.low = n0;
					rr.s.high = n1;
					*rp = rr.ll;
				}
			} else {
				UWtype m1, m0;
				/* Normalize.  */
				
				b = W_TYPE_SIZE - bm;
				
				d1 = (d1 << bm) | (d0 >> b);
				d0 = d0 << bm;
				n2 = n1 >> b;
				n1 = (n1 << bm) | (n0 >> b);
				n0 = n0 << bm;
		      
				udiv_qrnnd (q0, n1, n2, n1, d1);
				umul_ppmm (m1, m0, q0, d0);
		      
				if (m1 > n1 || (m1 == n1 && m0 > n0)) {
					q0--;
					sub_ddmmss (m1, m0, m1, m0, d1, d0);
				}
		      
				q1 = 0;
		      
				/* Remainder in (n1n0 - m1m0) >> bm.  */
				if (rp != 0) {
					sub_ddmmss (n1, n0, n1, n0, m1, m0);
					rr.s.low = (n1 << b) | (n0 >> bm);
					rr.s.high = n1 >> bm;
					*rp = rr.ll;
				}
			}
		}
	}
  
	ww.s.low = q0; ww.s.high = q1;
	return ww.ll;
}

long long _div64 (long long u, long long v)
{
	long c = 0;
	DWunion uu = {.ll = u};
	DWunion vv = {.ll = v};
	DWtype w;
	
	if (uu.s.high < 0)
		c = ~c,
			uu.ll = -uu.ll;
	if (vv.s.high < 0)
		c = ~c,
			vv.ll = -vv.ll;
	
	w = _stp_udivmoddi4 (uu.ll, vv.ll, (UDWtype *) 0);
	if (c)
		w = -w;
	
	return w;
}

long long _mod64 (long long u, long long v)
{
	long c = 0;
	DWunion uu = {.ll = u};
	DWunion vv = {.ll = v};
	DWtype w;
	
	if (uu.s.high < 0)
		c = ~c,
			uu.ll = -uu.ll;
	if (vv.s.high < 0)
		vv.ll = -vv.ll;
	
	(void) _stp_udivmoddi4 (uu.ll, vv.ll, (UDWtype*)&w);
	if (c)
		w = -w;
	
	return w;
}
#endif /* __i386__ */

#endif /* _ARITH_C_ */
