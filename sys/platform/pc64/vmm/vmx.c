#include "vmm.h"

#include "vmx.h"

static int
vmx_init(void)
{
	return 0;
}

static int
vmx_clean(void)
{
	return 0;
}

static struct vmm_ctl ctl_vmx = {
	vmx_init,
	vmx_clean
};

struct vmm_ctl*
get_ctl_intel(void){
	return &ctl_vmx;
}
