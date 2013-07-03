#include "vmm.h"

#include <sys/systm.h>

static int
svm_init(void)
{
	kprintf("VMM: SVM not supported\n");
	return (ENODEV);
}

static int
svm_enable(void)
{
	kprintf("VMM: SVM not supported\n");
	return (ENODEV);
}

static int
svm_disable(void)
{
	kprintf("VMM: SVM not supported\n");
	return (ENODEV);
}

static int
svm_vminit(void)
{
	kprintf("VMM: SVM not supported\n");
	return (ENODEV);
}

static int
svm_vmdestroy(void)
{
	kprintf("VMM: SVM not supported\n");
	return (ENODEV);
}

static struct vmm_ctl ctl_svm = {
	.name = "SVM from AMD",
	.init = svm_init,
	.enable = svm_enable,
	.disable = svm_disable,
	.vminit = svm_vminit,
	.vmdestroy = svm_vmdestroy,
};

struct vmm_ctl*
get_ctl_amd(void){
	return &ctl_svm;
}
