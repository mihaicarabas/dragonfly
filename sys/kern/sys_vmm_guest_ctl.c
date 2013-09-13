#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/vmm_guest_ctl.h>

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
	int vmm_error = 0;
	struct guest_options options;
	struct trapframe *tf = uap->sysmsg_frame;
	unsigned long stack_limit = USRSTACK;
	unsigned char stack_page[PAGE_SIZE];

	clear_quickret();

	switch (uap->op) {
		case VMM_GUEST_RUN:
			error = copyin(uap->options, &options, sizeof(struct guest_options));
			if (error) {
				kprintf("sys_vmm_guest: error copyin guest_options\n");
				goto out;
			}

			while(stack_limit > tf->tf_sp) {
				stack_limit -= PAGE_SIZE;
				options.new_stack -= PAGE_SIZE;

				error = copyin((const void *)stack_limit, (void *)stack_page, PAGE_SIZE);
				if (error) {
					kprintf("sys_vmm_guest: error copyin stack\n");
					goto out;
				}

				error = copyout((const void *)stack_page, (void *)options.new_stack, PAGE_SIZE);
				if (error) {
					kprintf("sys_vmm_guest: error copyout stack\n");
					goto out;
				}
			}

			bcopy(tf, &options.tf, sizeof(struct trapframe));

			error = vmm_vminit(&options);
			if (error) {
				if (error == ENODEV) {
					kprintf("sys_vmm_guest: vmm_vminit failed -"
					    "no VMM available \n");
					goto out;
				} else {
					kprintf("sys_vmm_guest: vmm_vminit failed\n");
					goto out_exit;
				}
			}

			generic_lwp_return(curthread->td_lwp, tf);

			vmm_error = vmm_vmrun();
			
			error = vmm_vmdestroy();
			if (error) {
				kprintf("sys_vmm_guest: vmm_vmdestroy failed\n");
				if (vmm_error)
					error = vmm_error;
				goto out_exit;
			}
			break;
		default:
			kprintf("sys_vmm_guest: INVALID op\n");
			error = EINVAL;
			goto out;
	}
out_exit:
	curproc->p_vmm = NULL;
	exit1(W_EXITCODE(error, 0));
out:
	return (error);
}

