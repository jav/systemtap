#include "sys/sdt.h"
	.file	"sdt_asm.c"
	.text
	.p2align 4,,15
.globl main
	.type	main, @function
main:
	.cfi_startproc
#if defined __x86_64__ || defined __i386__
	mov	$305419896, %eax
	STAP_PROBE3(provider,a,%ah,%al,%ax);
	mov	$305419896, %ebx
	STAP_PROBE3(provider,b,%bh,%bl,%bx);
	mov	$305419896, %ecx
	STAP_PROBE3(provider,c,%ch,%cl,%cx);
	mov	$305419896, %edx
	STAP_PROBE3(provider,d,%dh,%dl,%dx);
	mov	$305419896, %esi
	mov	$305419896, %edi
	STAP_PROBE1(provider,si,%si);
	STAP_PROBE1(provider,di,%di);
#endif	
#ifdef __x86_64__	
	STAP_PROBE1(provider,sil,%sil);
	STAP_PROBE1(provider,dil,%dil);
	mov	$305419896, %r8
	STAP_PROBE2(provider,r8,%r8b,%r8w);
	mov	$305419896, %r9
	STAP_PROBE2(provider,r9,%r9b,%r9w);
	mov	$305419896, %r10
	STAP_PROBE2(provider,r10,%r10b,%r10w);
	mov	$305419896, %r11
	STAP_PROBE2(provider,r11,%r11b,%r11w);
	mov	$305419896, %r12
	STAP_PROBE2(provider,r12,%r12b,%r12w);
	mov	$305419896, %r13
	STAP_PROBE2(provider,r13,%r13b,%r13w);
	mov	$305419896, %r14
	STAP_PROBE2(provider,r14,%r14b,%r14w);
	mov	$305419896, %r15
	STAP_PROBE2(provider,r15,%r15b,%r15w);
#endif	
	ret
	.cfi_endproc
