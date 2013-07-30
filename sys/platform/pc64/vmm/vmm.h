#ifndef _VMM_VMM_H_
#define _VMM_VMM_H_

#define MAX_NAME_LEN 256

#include <sys/param.h>
#include <sys/vmm_guest_ctl.h>

#define ERROR_IF(func)					\
	do {						\
	if ((err = (func))) {				\
		kprintf("VMM: %s error at line: %d\n",	\
		   __func__, __LINE__);			\
		goto error;				\
	}						\
	} while(0)					\

#define ERROR2_IF(func)					\
	do {						\
	if ((err = (func))) {				\
		kprintf("VMM: %s error at line: %d\n",	\
		   __func__, __LINE__);			\
		goto error2;				\
	}						\
	} while(0)					\

#ifdef VMM_DEBUG
#define dkprintf(fmt, args...)		kprintf(fmt, ##args)
#else
#define dkprintf(fmt, args...)
#endif

#define INSTRUCTION_MAX_LENGTH		15

struct vmm_ctl {
	char name[MAX_NAME_LEN];
	int (*init)(void);
	int (*enable)(void);
	int (*disable)(void);
	int (*vminit)(struct guest_options *);
	int (*vmdestroy)(void);
	int (*vmrun)(void);
	int (*vm_set_tls_area)(void);
};

struct vmm_ctl* get_ctl_intel(void);
struct vmm_ctl* get_ctl_amd(void);

#endif
