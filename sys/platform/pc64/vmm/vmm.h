#ifndef _VMM_VMM_H_
#define _VMM_VMM_H_

#define MAX_NAME_LEN 256

#include <sys/param.h>

struct vmm_ctl {
	char name[MAX_NAME_LEN];
	int (*init)(void);
	int (*enable)(void);
	int (*disable)(void);
	int (*vminit)(uint64_t rip, uint64_t rsp);
	int (*vmdestroy)(void);
	int (*vmrun)(void);
};

struct vmm_ctl* get_ctl_intel(void);
struct vmm_ctl* get_ctl_amd(void);

#endif
