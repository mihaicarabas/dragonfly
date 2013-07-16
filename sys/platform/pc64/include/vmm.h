#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

#include <sys/vmm_guest_ctl.h>

int vmm_vminit(struct guest_options*);
int vmm_vmdestroy(void);
int vmm_vmrun(void);

#endif
