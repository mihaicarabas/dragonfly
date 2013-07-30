#include <sys/vmm_guest_ctl.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/proc.h>

#include <sys/user.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <machine/cpu.h>
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
	struct guest_options options;
	clear_quickret();
	switch (uap->op) {
		case VMM_GUEST_INIT:
			error = copyin(uap->options, &options, sizeof(struct guest_options));
			if (error) {
				kprintf("sys_vmm_guest: error copyin guest_options\n");
				goto out;
			}

			error = vmm_vminit(&options);
			if (error) {
				kprintf("sys_vmm_guest: vmm_vminit failed\n");
				goto out;
			}
			break;
		case VMM_GUEST_RUN:
			error = vmm_vmrun();
			if (error) {
				kprintf("sys_vmm_guest: vmm_vmrun failed\n");
				goto out;
			}
			break;
		case VMM_GUEST_DESTROY:
			error = vmm_vmdestroy();
			if (error) {
				kprintf("sys_vmm_guest: vmm_vmdestroy failed\n");
				goto out;
			}
			break;
		default:
			kprintf("sys_vmm_guest: INVALID op\n");
			error = EINVAL;
			goto out;
	}
out:
	return (error);
}

