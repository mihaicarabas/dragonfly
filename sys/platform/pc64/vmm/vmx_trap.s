#include <machine/asmacros.h> 
#include "vmx_assym.h"


#define	VMX_RESTORE_GUEST(reg)			\
	movq VTI_GUEST_CR2(reg),%rsi;		\
	movq %rsi,%cr2;				\
	movq VTI_GUEST_RAX(reg),%rax;		\
	movq VTI_GUEST_RBX(reg),%rbx;		\
	movq VTI_GUEST_RCX(reg),%rcx;		\
	movq VTI_GUEST_RDX(reg),%rdx;		\
	movq VTI_GUEST_RSI(reg),%rsi;		\
	movq VTI_GUEST_RDI(reg),%rdi;		\
	movq VTI_GUEST_RBP(reg),%rbp;		\
	movq VTI_GUEST_R8(reg),%r8;		\
	movq VTI_GUEST_R9(reg),%r9;		\
	movq VTI_GUEST_R10(reg),%r10;		\
	movq VTI_GUEST_R11(reg),%r11;		\
	movq VTI_GUEST_R12(reg),%r12;		\
	movq VTI_GUEST_R13(reg),%r13;		\
	movq VTI_GUEST_R14(reg),%r14;		\
	movq VTI_GUEST_R15(reg),%r15;		\

#define	VMX_SAVE_GUEST(reg)			\
	movq %rax,VTI_GUEST_RAX(reg);		\
	movq %rbx,VTI_GUEST_RBX(reg);		\
	movq %rcx,VTI_GUEST_RCX(reg);		\
	movq %rdx,VTI_GUEST_RDX(reg);		\
	movq %rsi,VTI_GUEST_RSI(reg);		\
	movq %rdi,VTI_GUEST_RDI(reg);		\
	movq %rbp,VTI_GUEST_RBP(reg);		\
	movq %r8,VTI_GUEST_R8(reg);		\
	movq %r9,VTI_GUEST_R9(reg);		\
	movq %r10,VTI_GUEST_R10(reg);		\
	movq %r11,VTI_GUEST_R11(reg);		\
	movq %r12,VTI_GUEST_R12(reg);		\
	movq %r13,VTI_GUEST_R13(reg);		\
	movq %r14,VTI_GUEST_R14(reg);		\
	movq %r15,VTI_GUEST_R15(reg);		\
	movq %cr2, %rsi;			\
	movq %rsi, VTI_GUEST_CR2(reg);		\

#define	VMX_RUN_ERROR(dst_reg)			\
	jnc 	1f;				\
	movq 	$VM_FAIL_INVALID,dst_reg;	\
	jmp 	3f;				\
1:	jnz 	2f;				\
	movq 	$VM_FAIL_VALID,dst_reg;		\
	jmp 	3f;				\
2:	movq 	$VM_SUCCEED,dst_reg;		\
3:


.text

/*
 * void vmx_exit(void)
 * %rsp = vmx_thread_info
 */
ENTRY(vmx_vmexit)

	VMX_SAVE_GUEST(%rsp)

	movq	%rsp,%rdi

	movq VTI_HOST_RBX(%rdi),%rbx
	movq VTI_HOST_RBP(%rdi),%rbp
	movq VTI_HOST_R12(%rdi),%r12
	movq VTI_HOST_R13(%rdi),%r13
	movq VTI_HOST_R14(%rdi),%r14
	movq VTI_HOST_R15(%rdi),%r15
	movq VTI_HOST_RSP(%rdi),%rsp

	movq $VM_EXIT, %rax

	ret
END(vmx_vmexit)

/*
 * int vmx_launch(struct vmx_thread_info* vti)
 * %rdi = cti
 */
ENTRY(vmx_launch)
	movq %rbx,VTI_HOST_RBX(%rdi)
	movq %rbp,VTI_HOST_RBP(%rdi)
	movq %r12,VTI_HOST_R12(%rdi)
	movq %r13,VTI_HOST_R13(%rdi)
	movq %r14,VTI_HOST_R14(%rdi)
	movq %r15,VTI_HOST_R15(%rdi)
	movq %rsp,VTI_HOST_RSP(%rdi)

	movq %rdi,%rsp

	VMX_RESTORE_GUEST(%rsp)

	vmlaunch

	VMX_RUN_ERROR(%rax)

	movq	%rsp,%rdi

	movq VTI_HOST_RBX(%rdi),%rbx
	movq VTI_HOST_RBP(%rdi),%rbp
	movq VTI_HOST_R12(%rdi),%r12
	movq VTI_HOST_R13(%rdi),%r13
	movq VTI_HOST_R14(%rdi),%r14
	movq VTI_HOST_R15(%rdi),%r15
	movq VTI_HOST_RSP(%rdi),%rsp

	ret
END(vmx_launch)

/*
 * int vmx_resume(struct vmx_thread_info* vti)
 * %rdi = cti
 */
ENTRY(vmx_resume)
	movq %rbx,VTI_HOST_RBX(%rdi)
	movq %rbp,VTI_HOST_RBP(%rdi)
	movq %r12,VTI_HOST_R12(%rdi)
	movq %r13,VTI_HOST_R13(%rdi)
	movq %r14,VTI_HOST_R14(%rdi)
	movq %r15,VTI_HOST_R15(%rdi)
	movq %rsp,VTI_HOST_RSP(%rdi)

	movq %rdi,%rsp

	VMX_RESTORE_GUEST(%rsp)

	vmresume

	VMX_RUN_ERROR(%rax)

	movq	%rsp,%rdi

	movq VTI_HOST_RBX(%rdi),%rbx
	movq VTI_HOST_RBP(%rdi),%rbp
	movq VTI_HOST_R12(%rdi),%r12
	movq VTI_HOST_R13(%rdi),%r13
	movq VTI_HOST_R14(%rdi),%r14
	movq VTI_HOST_R15(%rdi),%r15
	movq VTI_HOST_RSP(%rdi),%rsp

	ret
END(vmx_resume)
