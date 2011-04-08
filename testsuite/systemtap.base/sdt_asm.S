#include "sys/sdt.h"
	.file	"sdt_asm.c"
	.text
	.p2align 4,,15
.globl main
	.type	main, @function
main:
	.cfi_startproc
#if defined __i386__
	pushl   %ebp
	movl    %esp, %ebp
	movl    $7, -4(%ebp)
	STAP_PROBE3(provider,memory,-4(%ebp),-8+4(%ebp),-8+3+1(%ebp));
#elif defined __x86_64__
	pushq    %rbp		
	mov     %rsp, %rbp
	movl    $7, -4(%rbp)
	STAP_PROBE3(provider,memory,-4(%rbp),-8+4(%rbp),-8+3+1(%rbp));
#endif
#if defined __i386__ || defined __x86_64__
	mov	$0x10203040, %eax
	STAP_PROBE4(provider,a,%ah,%al,%ax,%eax);
	mov	$0x11213141, %ebx
	STAP_PROBE4(provider,b,%bh,%bl,%bx,%ebx);
	mov	$0x12223242, %ecx
	STAP_PROBE4(provider,c,%ch,%cl,%cx,%ecx);
	mov	$0x13233343, %edx
	STAP_PROBE4(provider,d,%dh,%dl,%dx,%edx);
	mov	$0x14243444, %esi
	STAP_PROBE3(provider,si,%sil,%si,%esi);
	mov	$0x15253545, %edi
	STAP_PROBE3(provider,di,%dil,%di,%edi);
	mov	$0x16263646, %ebp
	STAP_PROBE2(provider,bp,%bp,%ebp);
	mov	$0x17273747, %esp
	STAP_PROBE2(provider,sp,%sp,%esp);
#endif	
#if defined __x86_64__
	mov	$0x1020304010203040, %rax
	STAP_PROBE1(provider,rax,%rax);
	mov	$0x1121314111213141, %rbx
	STAP_PROBE1(provider,rbx,%rbx);
	mov	$0x1222324212223242, %rcx
	STAP_PROBE1(provider,rcx,%rcx);
	mov	$0x1323334313233343, %rdx
	STAP_PROBE1(provider,rdx,%rdx);
	mov	$0x1424344414243444, %rsi
	STAP_PROBE1(provider,rsi,%rsi);
	mov	$0x1525354515253545, %rdi
	STAP_PROBE1(provider,rdi,%rdi);
	mov	$0x1626364616263646, %rbp
	STAP_PROBE1(provider,rbp,%rbp);
	mov	$0x1727374717273747, %rsp
	STAP_PROBE1(provider,rsp,%rsp);
	mov	$0x1828384818283848, %r8
	STAP_PROBE4(provider,r8,%r8b,%r8w,%r8d,%r8);
	mov	$0x1929394919293949, %r9
	STAP_PROBE4(provider,r9,%r9b,%r9w,%r9d,%r9);
	mov	$0x1a2a3a4a1a2a3a4a, %r10
	STAP_PROBE4(provider,r10,%r10b,%r10w,%r10d,%r10);
	mov	$0x1b2b3b4b1b2b3b4b, %r11
	STAP_PROBE4(provider,r11,%r11b,%r11w,%r11d,%r11);
	mov	$0x1c2c3c4c1c2c3c4c, %r12
	STAP_PROBE4(provider,r12,%r12b,%r12w,%r12d,%r12);
	mov	$0x1d2d3d4d1d2d3d4d, %r13
	STAP_PROBE4(provider,r13,%r13b,%r13w,%r13d,%r13);
	mov	$0x1e2e3e4e1e2e3e4e, %r14
	STAP_PROBE4(provider,r14,%r14b,%r14w,%r14d,%r14);
	mov	$0x1f2f3f4f1f2f3f4f, %r15
	STAP_PROBE4(provider,r15,%r15b,%r15w,%r15d,%r15);
#endif	
	ret
	.cfi_endproc
