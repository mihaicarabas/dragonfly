#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

#include <sys/vmm_guest_ctl.h>

static __inline
int vmm_vminit(struct guest_options*) {
	return 0;
}

static __inline
int vmm_vmdestroy(void) {
	return 0;
}

static __inline
int vmm_vmrun(void) {
	return 0;
}

static __inline
int vmm_vm_set_tls_area(void) {
	return 0;
}

static __inline
void vmm_lwp_return(struct lwp *lp, struct trapframe *frame) {
}

static __inline
void vmm_vm_set_guest_cr3(register_t guest_cr3) {
}

#endif
