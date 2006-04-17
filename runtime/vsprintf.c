/* -*- linux-c -*-
 * vsprintf.c
 * Copyright (C) 2006 Red Hat Inc.
 * Based on code from the Linux kernel
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _VSPRINTF_C_
#define _VSPRINTF_C_

enum endian {STP_NATIVE=0, STP_LITTLE, STP_BIG};
static enum endian _stp_endian = STP_NATIVE;

static int skip_atoi(const char **s)
{
	int i=0;
	while (isdigit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}

enum print_flag {STP_ZEROPAD=1, STP_SIGN=2, STP_PLUS=4, STP_SPACE=8, STP_LEFT=16, STP_SPECIAL=32, STP_LARGE=64};

static char * number(char * buf, char * end, uint64_t num, int base, int size, int precision, enum print_flag type)
{
	char c,sign,tmp[66];
	const char *digits;
	static const char small_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	static const char large_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int i;

	digits = (type & STP_LARGE) ? large_digits : small_digits;
	if (type & STP_LEFT)
		type &= ~STP_ZEROPAD;
	if (base < 2 || base > 36)
		return NULL;
	c = (type & STP_ZEROPAD) ? '0' : ' ';
	sign = 0;
	if (type & STP_SIGN) {
		if ((int64_t) num < 0) {
			sign = '-';
			num = - (int64_t) num;
			size--;
		} else if (type & STP_PLUS) {
			sign = '+';
			size--;
		} else if (type & STP_SPACE) {
			sign = ' ';
			size--;
		}
	}
	if (type & STP_SPECIAL) {
		if (base == 16)
			size -= 2;
		else if (base == 8)
			size--;
	}
	i = 0;
	if (num == 0)
		tmp[i++]='0';
	else while (num != 0)
		tmp[i++] = digits[do_div(num,base)];
	if (i > precision)
		precision = i;
	size -= precision;
	if (!(type&(STP_ZEROPAD+STP_LEFT))) {
		while(size-->0) {
			if (buf <= end)
				*buf = ' ';
			++buf;
		}
	}
	if (sign) {
		if (buf <= end)
			*buf = sign;
		++buf;
	}
	if (type & STP_SPECIAL) {
		if (base==8) {
			if (buf <= end)
				*buf = '0';
			++buf;
		} else if (base==16) {
			if (buf <= end)
				*buf = '0';
			++buf;
			if (buf <= end)
				*buf = digits[33];
			++buf;
		}
	}
	if (!(type & STP_LEFT)) {
		while (size-- > 0) {
			if (buf <= end)
				*buf = c;
			++buf;
		}
	}
	while (i < precision--) {
		if (buf <= end)
			*buf = '0';
		++buf;
	}
	while (i-- > 0) {
		if (buf <= end)
			*buf = tmp[i];
		++buf;
	}
	while (size-- > 0) {
		if (buf <= end)
			*buf = ' ';
		++buf;
	}
	return buf;
}

int _stp_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int len;
	uint64_t num;
	int i, base;
	char *str, *end, c;
	const char *s;
	enum print_flag flags;		/* flags to number() */
	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields */
	char *write_len_ptr = NULL;
	int write_len_width = 0;

	/* Reject out-of-range values early */
	if (unlikely((int) size < 0))
		return 0;

	str = buf;
	end = buf + size - 1;

	for (; *fmt ; ++fmt) {
		if (*fmt != '%') {
			if (str <= end)
				*str = *fmt;
			++str;
			continue;
		}

		/* process flags */
		flags = 0;
	repeat:
		++fmt;          /* this also skips first '%' */
		switch (*fmt) {
		case '-': flags |= STP_LEFT; goto repeat;
		case '+': flags |= STP_PLUS; goto repeat;
		case ' ': flags |= STP_SPACE; goto repeat;
		case '#': flags |= STP_SPECIAL; goto repeat;
		case '0': flags |= STP_ZEROPAD; goto repeat;
		}
		
		/* get field width */
		field_width = -1;
		if (isdigit(*fmt))
			field_width = skip_atoi(&fmt);
		else if (*fmt == '*') {
			++fmt;
			/* it's the next argument */
			field_width = va_arg(args, int);
			if (field_width < 0) {
				field_width = -field_width;
				flags |= STP_LEFT;
			}
		}

		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;	
			if (isdigit(*fmt))
				precision = skip_atoi(&fmt);
			else if (*fmt == '*') {
				++fmt;
				/* it's the next argument */
				precision = va_arg(args, int);
			}
			if (precision < 0)
				precision = 0;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
			if (qualifier == 'l' && *fmt == 'l') {
				qualifier = 'L';
				++fmt;
			}
		}

		/* default base */
		base = 10;

		switch (*fmt) {
		case 'b':
			num = va_arg(args, int64_t);
			switch(field_width) {
			case 1:
				if(str <= end)
					*(int8_t *)str = (int8_t)num;
				++str;
				break;
			case 2:
				if (_stp_endian != STP_NATIVE) {
					if (_stp_endian == STP_BIG)
						num = cpu_to_be16(num);
					else
						num = cpu_to_le16(num);						
				}
				if((str + 1) <= end)
					*(int16_t *)str = (int16_t)num;
				str+=2;
				break;
			case 8:
				if (_stp_endian != STP_NATIVE) {
					if (_stp_endian == STP_BIG)
						num = cpu_to_be64(num);
					else
						num = cpu_to_le64(num);						
				}

				if((str + 7) <= end)
					*(int64_t *)str = num;
				str+=8;
				break;
			case 4:
			default: // "%4b" by default
				if (_stp_endian != STP_NATIVE) {
					if (_stp_endian == STP_BIG)
						num = cpu_to_be32(num);
					else
						num = cpu_to_le32(num);						
				}

				if((str + 3) <= end)
					*(int32_t *)str = num;
				str+=4;
				break;
			}
			continue;
			
		case 's':
			s = va_arg(args, char *);
			if ((unsigned long)s < PAGE_SIZE)
				s = "<NULL>";

			len = strnlen(s, precision);

			if (!(flags & STP_LEFT)) {
				while (len < field_width--) {
					if (str <= end)
						*str = ' ';
					++str;
				}
			}
			for (i = 0; i < len; ++i) {
				if (str <= end)
					*str = *s;
				++str; ++s;
			}
			while (len < field_width--) {
				if (str <= end)
					*str = ' ';
				++str;
			}
			continue;

		case 'X':
			flags |= STP_LARGE;
		case 'x':
			base = 16;
			break;

		case 'd':
		case 'i':
			flags |= STP_SIGN;
		case 'u':
			break;
				
		case 'p':
			if (field_width == -1) {
				field_width = 2*sizeof(void *);
				flags |= STP_ZEROPAD;
			}
			str = number(str, end,
				     (unsigned long) va_arg(args, void *),
				     16, field_width, precision, flags);
			continue;

		case 'n':
			write_len_ptr = str;
			write_len_width = 2;
			if (field_width == 1)
				write_len_width = 1;
			else if (field_width == 4)
				write_len_width = 4;
			str += write_len_width;
			continue;
			
		case '%':
			if (str <= end)
				*str = '%';
			++str;
			continue;

			/* integer number formats - set up the flags and "break" */
		case 'o':
			base = 8;
			break;

		case 'c':
			if (!(flags & STP_LEFT)) {
				while (--field_width > 0) {
					if (str <= end)
						*str = ' ';
					++str;
				}
			}
			c = (unsigned char) va_arg(args, int);
			if (str <= end)
				*str = c;
			++str;
			while (--field_width > 0) {
				if (str <= end)
					*str = ' ';
				++str;
			}
			continue;

		default:
			if (str <= end)
				*str = '%';
			++str;
			if (*fmt) {
				if (str <= end)
					*str = *fmt;
				++str;
			} else {
				--fmt;
			}
			continue;
		}

		if (qualifier == 'L')
			num = va_arg(args, int64_t);
		else if (qualifier == 'l') {
			num = va_arg(args, unsigned long);
			if (flags & STP_SIGN)
				num = (signed long) num;
		} else if (qualifier == 'h') {
			num = (unsigned short) va_arg(args, int);
			if (flags & STP_SIGN)
				num = (signed short) num;
		} else {
			num = va_arg(args, unsigned int);
			if (flags & STP_SIGN)
				num = (signed int) num;
		}
		str = number(str, end, num, base,
			     field_width, precision, flags);
	}

	if (write_len_ptr) {
		int written;
		if (likely(str <= end))
			written = str - write_len_ptr - write_len_width;
		else
			written = end - write_len_ptr - write_len_width;

		if (likely(write_len_ptr + write_len_width < end)) {
			switch (write_len_width) {
			case 1: 
				*(uint8_t *)write_len_ptr = (uint8_t)written;
				break;
			case 2: 
				*(uint16_t *)write_len_ptr = (uint16_t)written;
				break;

			case 4: 
				*(uint32_t *)write_len_ptr = (uint32_t)written;
				break;
			}
		}
	}
	
	if (likely(str <= end))
		*str = '\0';
	else if (size > 0)
		/* don't write out a null byte if the buf size is zero */
		*end = '\0';
	return str-buf;
}

int _stp_vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i = vsnprintf(buf,size,fmt,args);
	return (i >= size) ? (size - 1) : i;
}

int _stp_snprintf(char *buf, size_t size, const char *fmt, ...)
{
        va_list args;
        int i;

        va_start(args, fmt);
        i=_stp_vsnprintf(buf,size,fmt,args);
        va_end(args);
        return i;
}

#endif /* _VSPRINTF_C_ */
