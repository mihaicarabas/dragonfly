#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>

#include "vmx.h"
#include "vmx_instr.h"

ASSYM(VTI_GUEST_RAX, offsetof(struct vmx_thread_info, guest.tf_rax));
ASSYM(VTI_GUEST_RBX, offsetof(struct vmx_thread_info, guest.tf_rbx));
ASSYM(VTI_GUEST_RCX, offsetof(struct vmx_thread_info, guest.tf_rcx));
ASSYM(VTI_GUEST_RDX, offsetof(struct vmx_thread_info, guest.tf_rdx));
ASSYM(VTI_GUEST_RSI, offsetof(struct vmx_thread_info, guest.tf_rsi));
ASSYM(VTI_GUEST_RDI, offsetof(struct vmx_thread_info, guest.tf_rdi));
ASSYM(VTI_GUEST_RBP, offsetof(struct vmx_thread_info, guest.tf_rbp));
ASSYM(VTI_GUEST_R8, offsetof(struct vmx_thread_info, guest.tf_r8));
ASSYM(VTI_GUEST_R9, offsetof(struct vmx_thread_info, guest.tf_r9));
ASSYM(VTI_GUEST_R10, offsetof(struct vmx_thread_info, guest.tf_r10));
ASSYM(VTI_GUEST_R11, offsetof(struct vmx_thread_info, guest.tf_rdi));
ASSYM(VTI_GUEST_R12, offsetof(struct vmx_thread_info, guest.tf_r12));
ASSYM(VTI_GUEST_R13, offsetof(struct vmx_thread_info, guest.tf_r13));
ASSYM(VTI_GUEST_R14, offsetof(struct vmx_thread_info, guest.tf_r14));
ASSYM(VTI_GUEST_R15, offsetof(struct vmx_thread_info, guest.tf_r15));
ASSYM(VTI_GUEST_CR2, offsetof(struct vmx_thread_info, guest_cr2));

ASSYM(VTI_HOST_RBX, offsetof(struct vmx_thread_info, host_rbx));
ASSYM(VTI_HOST_RBP, offsetof(struct vmx_thread_info, host_rbp));
ASSYM(VTI_HOST_R10, offsetof(struct vmx_thread_info, host_r10));
ASSYM(VTI_HOST_R11, offsetof(struct vmx_thread_info, host_r11));
ASSYM(VTI_HOST_R12, offsetof(struct vmx_thread_info, host_r12));
ASSYM(VTI_HOST_R13, offsetof(struct vmx_thread_info, host_r13));
ASSYM(VTI_HOST_R14, offsetof(struct vmx_thread_info, host_r14));
ASSYM(VTI_HOST_R15, offsetof(struct vmx_thread_info, host_r15));
ASSYM(VTI_HOST_RSP, offsetof(struct vmx_thread_info, host_rsp));

ASSYM(VM_SUCCEED, VM_SUCCEED);
ASSYM(VM_FAIL_INVALID, VM_FAIL_INVALID);
ASSYM(VM_FAIL_VALID, VM_FAIL_VALID);
ASSYM(VM_EXIT, VM_EXIT);

