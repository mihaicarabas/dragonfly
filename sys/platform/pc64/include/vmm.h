#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

#include <sys/vmm_guest_ctl.h>

int vmm_vminit(struct guest_options*);
int vmm_vmdestroy(void);
int vmm_vmrun(void);
int vmm_vm_set_tls_area(void);
void vmm_lwp_return(struct lwp *lp, struct trapframe *frame);

#endif
