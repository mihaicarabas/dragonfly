#include <sys/vmm_guest_ctl.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/proc.h>

#include <sys/user.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/vmm.h>

/*
 * vmm guest system call:
 * - init the calling thread structure
 * - prepare for running in non-root mode
 */
int
sys_vmm_guest_ctl(struct vmm_guest_ctl_args *uap)
{
	int error = 0;

	if (!vmm_enabled) {
		kprintf("sys_vmm_guest: VMM not enabled\n");
		error = ENODEV;
		goto out;
	}

	switch (uap->operation) {
		case VMM_GUEST_INIT:
			kprintf("sys_vmm_guest: VMM_GUEST_INIT op\n");
			break;
		default:
			kprintf("sys_vmm_guest: INVALID op\n");
			error = EINVAL;
	}
out:
	return (error);
}

