#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

int vmm_vminit(uint64_t rip, uint64_t rsp);
int vmm_vmdestroy(void);
int vmm_vmrun(void);

#endif
