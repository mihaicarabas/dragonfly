#ifndef	_SYS_VMM_GUEST_CTL_H_
#define	_SYS_VMM_GUEST_CTL_H_

/*
 * Init the calling thread with a VM context
 * Run the calling thread in VM context
 * Destroy the VM context of the thread
 */

#define		VMM_GUEST_INIT		0
#define		VMM_GUEST_RUN		1
#define		VMM_GUEST_DESTROY	2

#include <sys/types.h>

struct guest_options {
	register_t ip;
	register_t sp;
};


#ifdef _KERNEL

#else /* !_KERNEL */

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif

__BEGIN_DECLS


int	vmm_guest_ctl (int, struct guest_options *);
__END_DECLS

#endif /* !_KERNEL */
#endif	/* !_SYS_VMM_GUEST_CTL_H_ */
