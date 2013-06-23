#include "vmm.h"

#include <sys/systm.h>

static int
svm_init(void)
{
	kprintf("Operation not supported\n");
	return (ENODEV);
}

static int
svm_clean(void)
{
	kprintf("Operation not supported\n");
	return (ENODEV);
}


static struct vmm_ctl ctl_svm = {
	svm_init,
	svm_clean
};

struct vmm_ctl*
get_ctl_amd(void){
	return &ctl_svm;
}
