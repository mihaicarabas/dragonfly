#ifndef _VMM_VMM_H_
#define _VMM_VMM_H_

#define MAX_NAME_LEN 256

struct vmm_ctl {
	char name[MAX_NAME_LEN];
	int (*init)(void);
	int (*enable)(void);
	int (*disable)(void);
	int (*vminit)(void);
	int (*vmdestroy)(void);
};

struct vmm_ctl* get_ctl_intel(void);
struct vmm_ctl* get_ctl_amd(void);

#endif
