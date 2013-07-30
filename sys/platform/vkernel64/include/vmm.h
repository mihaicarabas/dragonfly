#ifndef _MACHINE_VMM_H_
#define _MACHINE_VMM_H_

#include <sys/vmm_guest_ctl.h>

static inline int
vmm_vminit(struct guest_options *go)
{
	return -1;
}

static inline int
vmm_vmdestroy(void)
{
	return -1;
}

static inline int
vmm_vmrun(void)
{
	return -1;
}

static inline int
vmm_vm_set_tls_area(void)
{
	return -1;
}
#endif
