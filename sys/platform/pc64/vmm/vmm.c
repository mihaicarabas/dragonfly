#include "vmm.h"

#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cputypes.h>
#include <machine/vmm.h>
#include <machine/md_var.h>

static struct vmm_ctl *ctl = NULL;

struct sysctl_ctx_list vmm_sysctl_ctx;
struct sysctl_oid *vmm_sysctl_tree;

static int vmm_enabled;

static int
sysctl_vmm_enable(SYSCTL_HANDLER_ARGS)
{
	int error, new_val;

	new_val = vmm_enabled;

	error = sysctl_handle_int(oidp, &new_val, 0, req);
        if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val != 0 && new_val != 1)
		return (EINVAL);

	if (vmm_enabled != new_val) {
		if (new_val == 1) {
			if (ctl->enable()) {
				kprintf("VMM: vmm enable() failed\n");
				return (EINVAL);
			}
		} else if (new_val == 0) {
			if (ctl->disable()) {
				kprintf("VMM: vmm disable() failed\n");
				return (EINVAL);
			}
		}
	} else {
		return (EINVAL);
	}

	vmm_enabled = new_val;

	return (0);
}

static void
vmm_init(void)
{
	sysctl_ctx_init(&vmm_sysctl_ctx);
	vmm_sysctl_tree = SYSCTL_ADD_NODE(&vmm_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw),
	    OID_AUTO, "vmm",
	    CTLFLAG_RD, 0, "VMM options");

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		ctl = get_ctl_intel();
	} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
		ctl = get_ctl_amd();
	}

	if (ctl->init()) {
		SYSCTL_ADD_STRING(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "enable", CTLFLAG_RD,
		    "NOT SUPPORTED", 0,
		    "enable not supported");
	} else {
		SYSCTL_ADD_STRING(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "type", CTLFLAG_RD,
		    ctl->name, 0,
		    "Type of the VMM");
		SYSCTL_ADD_PROC(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "enable", CTLTYPE_INT | CTLFLAG_WR,
		    NULL, sizeof vmm_enabled, sysctl_vmm_enable, "I",
		    "Control the state of the VMM");

		if (ctl->enable()) {
			kprintf("VMM: vmm enable() failed\n");
		} else {
			vmm_enabled = 1;
		}
	}
}
SYSINIT(vmm, SI_BOOT2_CPU_TOPOLOGY, SI_ORDER_ANY, vmm_init, NULL)
