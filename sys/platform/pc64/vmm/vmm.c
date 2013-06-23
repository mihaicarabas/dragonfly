#include "vmm.h"

#include <sys/systm.h>

#include <machine/cputypes.h>
#include <machine/vmm.h>
#include <machine/md_var.h>

static struct vmm_ctl *ctl = NULL;

int
vmm_init(void)
{
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		ctl = get_ctl_intel();
	} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
		ctl = get_ctl_amd();
	}

	return ctl->init();
}

int
vmm_clean(void)
{
	if (ctl == NULL) {
		kprintf("vmm_clean: no vmm_init was performed\n");
		return (ENXIO);
	}

	return ctl->clean();
}
