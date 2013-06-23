#ifndef _VMM_VMM_H_
#define _VMM_VMM_H_

struct vmm_ctl {
	int (*init)(void);
	int (*clean)(void);
};

struct vmm_ctl* get_ctl_intel(void);
struct vmm_ctl* get_ctl_amd(void);

#endif
