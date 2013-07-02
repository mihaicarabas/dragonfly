#ifndef	_SYS_VMM_GUEST_CTL_H_
#define	_SYS_VMM_GUEST_CTL_H_

/*
 * Init the calling thread for VMM operations
 * Set the calling thread as a VMM thread
 * When switchin to this thread, enter non-root
 * operation mode
 */
#define		VMM_GUEST_INIT		0

#ifdef _KERNEL

#else /* !_KERNEL */

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif

__BEGIN_DECLS
int	vmm_guest_ctl (int);
__END_DECLS

#endif /* !_KERNEL */
#endif	/* !_SYS_VMM_GUEST_CTL_H_ */


