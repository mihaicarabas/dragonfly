#ifndef	_SYS_VMM_GUEST_CTL_H_
#define	_SYS_VMM_GUEST_CTL_H_

/*
 * Init the calling thread with a VM context
 * Run the calling thread in VM context
 * Destroy the VM context of the thread
 */

#define		VMM_GUEST_RUN		1

#include <sys/types.h>
#include <machine/frame.h>

struct guest_options {
	register_t guest_cr3;
	register_t vmm_cr3;
	struct trapframe tf;
	uint8_t master;
};


int	vmm_guest_ctl (int, struct guest_options *);
#endif	/* !_SYS_VMM_GUEST_CTL_H_ */
