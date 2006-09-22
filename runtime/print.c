/* -*- linux-c -*- 
 * Print Functions
 * Copyright (C) 2005, 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _PRINT_C_
#define _PRINT_C_

#include "string.h"
#include "io.c"
#include "vsprintf.c"

/** @file print.c
 * Printing Functions.
 */

/** @addtogroup print Print Functions
 * The print buffer is for collecting output to send to the user daemon.
 * This is a per-cpu static buffer.  The buffer is sent when
 * _stp_print_flush() is called.
 *
 * The reason to do this is to allow multiple small prints to be combined then
 * timestamped and sent together to stpd. This is more efficient than sending
 * numerous small packets.
 *
 * This function is called automatically when the print buffer is full.
 * It MUST also be called at the end of every probe that prints something.
 * @{
 */

#ifdef STP_RELAYFS
#define STP_TIMESTAMP_SIZE (sizeof(uint32_t))
#else
#define STP_TIMESTAMP_SIZE 0
#endif /* STP_RELAYFS */


#define STP_PRINT_BUF_START (STP_TIMESTAMP_SIZE)
#ifndef STP_PRINT_BUF_LEN
#define STP_PRINT_BUF_LEN 8192
#endif

typedef struct __stp_pbuf {
	uint32_t len;			/* bytes used in the buffer */
	char timestamp[STP_TIMESTAMP_SIZE];
	char buf[STP_PRINT_BUF_LEN];
} _stp_pbuf;

void *Stp_pbuf = NULL;

int _stp_print_init (void)
{
	Stp_pbuf = alloc_percpu(_stp_pbuf);
	if (unlikely(Stp_pbuf == 0))
		return -1;
	return 0;
}

void _stp_print_cleanup (void)
{
	if (Stp_pbuf)
		free_percpu(Stp_pbuf);
}

/** Send the print buffer to the transport now.
 * Output accumulates in the print buffer until it
 * is filled, or this is called. This MUST be called before returning
 * from a probe or accumulated output in the print buffer will be lost.
 *
 * @note Preemption must be disabled to use this.
 */
void _stp_print_flush (void)
{
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());

	/* check to see if there is anything in the buffer */
	if (pb->len == 0)
		return;

#ifdef STP_RELAYFS_MERGE
	/* In merge-mode, stpd expects relayfs data to start with a 4-byte length */
	/* followed by a 4-byte sequence number. In non-merge mode, anything goes. */

	*((uint32_t *)pb->timestamp) = _stp_seq_inc();

	if (unlikely(_stp_transport_write(pb, pb->len+4+STP_TIMESTAMP_SIZE) < 0))
		atomic_inc (&_stp_transport_failures);
#else
	if (unlikely(_stp_transport_write(pb->buf, pb->len) < 0))
		atomic_inc (&_stp_transport_failures);
#endif

	pb->len = 0;
}

#ifndef STP_MAXBINARYARGS
#define STP_MAXBINARYARGS 127
#endif


/** Reserves space in the output buffer for direct I/O.
 */

#if defined STP_RELAYFS && !defined STP_RELAYFS_MERGE
static void * _stp_reserve_bytes (int numbytes)
{
	if (unlikely(numbytes == 0))
		return NULL;
	_stp_print_flush();
	return relay_reserve(_stp_chan, numbytes);
}
#else
static void * _stp_reserve_bytes (int numbytes)
{
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	int size = STP_PRINT_BUF_LEN - pb->len;
	void * ret;

	if (unlikely(numbytes == 0 || numbytes > STP_PRINT_BUF_LEN))
		return NULL;

	if (numbytes > size)
		_stp_print_flush();

	ret = pb->buf + pb->len;
	pb->len += numbytes;
	return ret;
}
#endif /* STP_RELAYFS */

/** Write 64-bit args directly into the output stream.
 * This function takes a variable number of 64-bit arguments
 * and writes them directly into the output stream.  Marginally faster
 * than doing the same in _stp_vsnprintf().
 * @sa _stp_vsnprintf()
 */
static void _stp_print_binary (int num, ...)
{
	va_list vargs;
	int i;
	int64_t *args;
	
	if (unlikely(num > STP_MAXBINARYARGS))
		num = STP_MAXBINARYARGS;

	args = _stp_reserve_bytes(num * sizeof(int64_t));

	if (args != NULL) {
		va_start(vargs, num);
		for (i = 0; i < num; i++) {
			args[i] = va_arg(vargs, int64_t);
		}
		va_end(vargs);
	}
}

/** Print into the print buffer.
 * Like printf, except output goes to the print buffer.
 * Safe because overflowing the buffer is not allowed.
 *
 * @sa _stp_print_flush()
 */
#define _stp_printf(args...) _stp_sprintf(_stp_stdout,args)

/** Print into the print buffer.
 * Use this if your function already has a va_list.
 * You probably want _stp_printf().
 */

#define _stp_vprintf(fmt,args) _stp_vsprintf(_stp_stdout,fmt,args)

/** Write a C string into the print buffer.
 * Copies a string into a print buffer.
 * Safe because overflowing the buffer is not allowed.
 * This is more efficient than using _stp_printf() if you don't
 * need fancy formatting.
 *
 * @param str A C string.
 * @sa _stp_print
 */
#define _stp_print_cstr(str) _stp_string_cat_cstr(_stp_stdout,str)


/** Write a String into the print buffer.
 * Copies a String into a print buffer.
 * Safe because overflowing the buffer is not allowed.
 * This is more efficient than using _stp_printf() if you don't
 * need fancy formatting.
 *
 * @param str A String.
 * @sa _stp_print
 */
#define _stp_print_string(str) _stp_string_cat_string(_stp_stdout,str)

/* This function is used when printing maps or stats. */
/* Probably belongs elsewhere, but is here for now. */
/* It takes a format specification like those used for */
/* printing maps and stats. It prints chars until it sees */
/* a special format char (beginning with '%'. Then it */
/* returns a pointer to that. */
static char *next_fmt(char *fmt, int *num)
{
	char *f = fmt;
	int in_fmt = 0;
	*num = 0;
	while (*f) {
		if (in_fmt) {
			if (*f == '%') {
				_stp_string_cat_char(_stp_stdout,'%');
				in_fmt = 0;
			} else if (*f > '0' && *f <= '9') {
				*num = *f - '0';
				f++;
				return f;
			} else
				return f;
		} else if (*f == '%')
			in_fmt = 1;
		else
			_stp_string_cat_char(_stp_stdout,*f);
		f++;
	}
	return f;
}

/** Write a String or C string into the print buffer.
 * This macro selects the proper function to call.
 * @param str A String or C string (char *)
 * @sa _stp_print_cstr _stp_print_string
 */

#define _stp_print(str)							\
	({								\
	  if (__builtin_types_compatible_p (typeof (str), char[])) {	\
		  char *x = (char *)str;				\
		  _stp_string_cat_cstr(_stp_stdout,x);			\
	  } else {							\
		  String x = (String)str;				\
		  _stp_string_cat_string(_stp_stdout,x);		\
	  }								\
  })

/** @} */
#endif /* _PRINT_C_ */
