long _stp_strncpy_from_user(char *dst, const char __user *src, long count);
//static long __stp_strncpy_from_user(char *dst, const char __user *src, long count);

#if defined (__i386__)
#define __stp_strncpy_from_user(dst,src,count,res)			   \
do {									   \
	int __d0, __d1, __d2;						   \
	__asm__ __volatile__(						   \
		"	testl %1,%1\n"					   \
		"	jz 2f\n"					   \
		"0:	lodsb\n"					   \
		"	stosb\n"					   \
		"	testb %%al,%%al\n"				   \
		"	jz 1f\n"					   \
		"	decl %1\n"					   \
		"	jnz 0b\n"					   \
		"1:	subl %1,%0\n"					   \
		"2:\n"							   \
		".section .fixup,\"ax\"\n"				   \
		"3:	movl %5,%0\n"					   \
		"	jmp 2b\n"					   \
		".previous\n"						   \
		".section __ex_table,\"a\"\n"				   \
		"	.align 4\n"					   \
		"	.long 0b,3b\n"					   \
		".previous"						   \
		: "=d"(res), "=c"(count), "=&a" (__d0), "=&S" (__d1),	   \
		  "=&D" (__d2)						   \
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src), "4"(dst) \
		: "memory");						   \
} while (0)
#elif defined (__x86_64__)
#define __stp_strncpy_from_user(dst,src,count,res)			   \
do {									   \
	long __d0, __d1, __d2;						   \
	__asm__ __volatile__(						   \
		"	testq %1,%1\n"					   \
		"	jz 2f\n"					   \
		"0:	lodsb\n"					   \
		"	stosb\n"					   \
		"	testb %%al,%%al\n"				   \
		"	jz 1f\n"					   \
		"	decq %1\n"					   \
		"	jnz 0b\n"					   \
		"1:	subq %1,%0\n"					   \
		"2:\n"							   \
		".section .fixup,\"ax\"\n"				   \
		"3:	movq %5,%0\n"					   \
		"	jmp 2b\n"					   \
		".previous\n"						   \
		".section __ex_table,\"a\"\n"				   \
		"	.align 8\n"					   \
		"	.quad 0b,3b\n"					   \
		".previous"						   \
		: "=r"(res), "=c"(count), "=&a" (__d0), "=&S" (__d1),	   \
		  "=&D" (__d2)						   \
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src), "4"(dst) \
		: "memory");						   \
} while (0)
#endif

/** Copy a NULL-terminated string from userspace.
 * On success, returns the length of the string (not including the trailing
 * NULL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 * @param dst Destination address, in kernel space.  This buffer must be at
 *         least <i>count</i> bytes long.
 * @param src Source address, in user space.
 * @param count Maximum number of bytes to copy, including the trailing NULL.
 * 
 * If <i>count</i> is smaller than the length of the string, copies 
 * <i>count</i> bytes and returns <i>count</i>.
 */


long
_stp_strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res;
	__stp_strncpy_from_user(dst, src, count, res);
	return res;
}

/** Copy a block of data from user space.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.

 * @param dst Destination address, in kernel space.
 * @param src Source address, in user space.
 * @param count Number of bytes to copy.
 * @return number of bytes that could not be copied. On success, 
 * this will be zero.
 *
 */

unsigned long inline
_stp_copy_from_user (char *dst, const char __user *src, unsigned long count)
{
	return __copy_from_user_inatomic(dst, src, count);
}

/** Copy an argv from user space to a List.
 *
 * @param list A list.
 * @param argv Source argv, in user space.
 * @return number of elements in <i>list</i>
 *
 * @b Example:
 * @include argv.c
 */

int _stp_copy_argv_from_user (MAP list, char __user *__user *argv)
{
	char str[128];
	char __user *vstr;
	int len;

	if (argv)
		argv++;

	while (argv != NULL) {
		if (get_user (vstr, argv))
			break;
		
		if (vstr == NULL)
			break;
		
		len = _stp_strncpy_from_user(str, vstr, 128);
		str[len] = 0;
		_stp_list_add_str (list, str);
		argv++;
	}
	return list->num;
}
