#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

#include <sys/vmm.h>

int vmm_vminit(struct guest_options*);
int vmm_vmdestroy(void);
int vmm_vmrun(void);
int vmm_vm_set_tls_area(void);
void vmm_lwp_return(struct lwp *lp, struct trapframe *frame);
void vmm_vm_set_guest_cr3(register_t guest_cr3);
int vmm_vm_get_gpa(register_t *gpa, register_t uaddr);

#endif
