#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>

#include "vmm.h"

#include "vmx.h"
#include "vmx_instr.h"

struct vmx_ctl_info vmx_pinbased = {
	.msr_addr = IA32_VMX_PINBASED_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_PINBASED_CTLS,
};

struct vmx_ctl_info vmx_procbased = {
	.msr_addr = IA32_VMX_PROCBASED_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_PROCBASED_CTLS,
};

struct vmx_ctl_info vmx_procbased2 = {
	.msr_addr = IA32_VMX_PROCBASED_CTLS2,
	.msr_true_addr = IA32_VMX_PROCBASED_CTLS2,
};

struct vmx_ctl_info vmx_exit = {
	.msr_addr = IA32_VMX_EXIT_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_EXIT_CTLS,
};

struct vmx_ctl_info vmx_entry = {
	.msr_addr = IA32_VMX_ENTRY_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_ENTRY_CTLS,
};

struct vmx_ctl_info vmx_ept_vpid_cap = {
	.msr_addr = IA32_VMX_EPT_VPID_CAP,
	.msr_true_addr = IA32_VMX_EPT_VPID_CAP,
};



struct vmx_pcpu_info *pcpu_info;

/* VMX set control setting
 * Intel System Programming Guide, Part 3, Order Number 326019
 * 31.5.1 Algorithms for Determining VMX Capabilities
 * Implement Algorithm 3
 */
static int
vmx_set_ctl_setting(struct vmx_ctl_info *vmx_ctl, uint32_t bit_no, setting_t value) {
	uint64_t vmx_basic;
	uint64_t ctl_val;

	/* Check if its branch b. or c. */
	vmx_basic = rdmsr(IA32_VMX_BASIC);
	if (IS_TRUE_CTL_AVAIL(vmx_basic))
		ctl_val = rdmsr(vmx_ctl->msr_true_addr);
	else
		ctl_val = rdmsr(vmx_ctl->msr_addr);
	
	/* Check if the value is known by VMM or set on DEFAULT */
	switch(value) {
		case DEFAULT:
			/* Both settings are allowd
			 * - step b.iii)
			 *   or
			 * - c.iii), c.iv)
			 */
			if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no)
			    && IS_ONE_SETTING_ALLOWED(ctl_val, bit_no)) {

				/* For c.iii) and c.iv) */
				if(IS_TRUE_CTL_AVAIL(vmx_basic))
					ctl_val = rdmsr(vmx_ctl->msr_addr);

				if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no))
					vmx_ctl->ctls |= ~BIT(bit_no);
				if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
					vmx_ctl->ctls &= BIT(bit_no);

			} else if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls |= ~BIT(bit_no);
			} else if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls &= BIT(bit_no);
			} else {
				return (EINVAL);
			}
			break;
		case ZERO:
			/* For b.ii) or c.ii) */
			if (!IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls |= ~BIT(bit_no);

			break;
		case ONE:
			/* For b.ii) or c.ii) */
			if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls &= BIT(bit_no);

			break;
	}
	return 0;
}

static void
vmx_set_default_settings(struct vmx_ctl_info *vmx_ctl)
{
	int i;

	for(i = 0; i < 32; i++) {
		vmx_set_ctl_setting(vmx_ctl, i, DEFAULT);
	}
}

static void
alloc_vmxon_regions(void)
{
	uint64_t vmx_basic_value;
	int cpu;

	vmx_basic_value = rdmsr(IA32_VMX_BASIC);
	pcpu_info = kmalloc(ncpus * sizeof(struct vmx_pcpu_info), M_TEMP, M_WAITOK | M_ZERO);

	kprintf("alloc_vmxon_regions: VMX_WIDHTH_ADDR: %x\n", (unsigned int)VMX_WIDTH_ADDR(vmx_basic_value));

	for (cpu = 0; cpu < ncpus; cpu++) {

		/* The address must be aligned to 4K - alloc extra */
		pcpu_info[cpu].vmxon_region = kmalloc(VMX_REGION(vmx_basic_value) + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

		/* Align address */
		pcpu_info[cpu].vmxon_region_aligned = (unsigned char*) VMXON_REGION_ALIGN(pcpu_info[cpu].vmxon_region);

		/* In the first 31 bits put the vmx revision*/
		*((uint32_t *) pcpu_info[cpu].vmxon_region_aligned) = VMX_REVISION(vmx_basic_value);
	}
}

static void
free_vmxon_regions(void)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		pcpu_info[i].vmxon_region_aligned = NULL;

		kfree(pcpu_info[i].vmxon_region, M_TEMP);
	}

	kfree(pcpu_info, M_TEMP);
}

static void
execute_vmxon(void *dummy)
{
	int err;
	uint64_t cr0_fixed_bits_to_1;
	uint64_t cr0_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_1;
	unsigned char *vmxon_region;

	/* A.7 VMX-FIXED BITS IN CR0 */
	cr0_fixed_bits_to_1 = rdmsr(IA32_VMX_CR0_FIXED0);
	cr0_fixed_bits_to_0 = rdmsr(IA32_VMX_CR0_FIXED1);
	load_cr0(rcr0() | (cr0_fixed_bits_to_1 & cr0_fixed_bits_to_0));

	/* A.8 VMX-FIXED BITS IN CR4 */
	cr4_fixed_bits_to_1 = rdmsr(IA32_VMX_CR0_FIXED0);
	cr4_fixed_bits_to_0 = rdmsr(IA32_VMX_CR0_FIXED1);
	load_cr4(rcr4() | (cr4_fixed_bits_to_1 & cr4_fixed_bits_to_0));

	/* Enable VMX */
	load_cr4(rcr4() | CR4_VMXE);

	vmxon_region = pcpu_info[mycpuid].vmxon_region_aligned;
	err = vmxon(vmxon_region);
	if (err) {
		kprintf("vmxon failed on cpu%d\n", mycpuid);
	}
}

static void
execute_vmxoff(void *dummy)
{
	vmxoff();

	/* Disable VMX */
	load_cr4(rcr4() & ~CR4_VMXE);
}

static int
vmx_init(void)
{
	uint64_t feature_control;
	int err;
	globaldata_t gd;
	int cpu, seq;


	/*
	 * The ability of a processor to support VMX operation
	 * and related instructions is indicated by:
	 * CPUID.1:ECX.VMX[bit 5] = 1
	 */
	if (!(cpu_feature2 & CPUID2_VMX)) {
		kprintf("VMX is not supported by this Intel CPU\n");
		return (ENODEV);
	}

	vmx_set_default_settings(&vmx_pinbased);
	vmx_set_default_settings(&vmx_procbased);
	vmx_set_default_settings(&vmx_procbased2);
	vmx_set_default_settings(&vmx_exit);
	vmx_set_default_settings(&vmx_entry);

	/* Enable second level for procbades */
	err = vmx_set_ctl_setting(&vmx_procbased,
	    PROCBASED_ACTIVATE_SECONDARY_CONTROLS,
	    ONE);
	if (err) {
		kprintf("PROCBASED_ACTIVATE_SECONDARY_CONTROLS not supported by this CPU\n");
		return (ENODEV);
	}

	/* Enable EPT feature */
	err = vmx_set_ctl_setting(&vmx_procbased2,
	    PROCBASED2_ENABLE_EPT,
	    ONE);
	if (err) {
		kprintf("PROCBASED2_ENABLE_EPT not supported by this CPU\n");
		return (ENODEV);
	}

	/* Enable VPID feature */
	err = vmx_set_ctl_setting(&vmx_procbased2,
	    PROCBASED2_ENABLE_VPID,
	    ONE);
	if (err) {
		kprintf("PROCBASED2_ENABLE_VPID not supported by this CPU\n");
		return (ENODEV);
	}


	/* Check for the feature control status */
	feature_control = rdmsr(IA32_FEATURE_CONTROL);
	if ((feature_control & FEATURE_CONTROL_LOCKED)) {
		kprintf("IA32_FEATURE_CONTROl is not locked\n");
		return (EINVAL);
	}
	if (!(feature_control & FEATURE_CONTROL_VMX_BIOS_ENABLED)) {
		kprintf("VMX is disable by the BIOS\n");
		return (EINVAL);
	}
	
	alloc_vmxon_regions();

	for (cpu = 0; cpu < ncpus; cpu++) {
		gd = globaldata_find(cpu);
		seq = lwkt_send_ipiq(gd, execute_vmxon, NULL);
		lwkt_wait_ipiq(gd, seq);
	}


	return 0;
}

static int
vmx_clean(void)
{
	globaldata_t gd;
	int cpu, seq;

	for (cpu = 0; cpu < ncpus; cpu++) {
		gd = globaldata_find(cpu);
		seq = lwkt_send_ipiq(gd, execute_vmxoff, NULL);
		lwkt_wait_ipiq(gd, seq);
	}

	free_vmxon_regions();
	return 0;
}

static struct vmm_ctl ctl_vmx = {
	vmx_init,
	vmx_clean
};

struct vmm_ctl*
get_ctl_intel(void){
	return &ctl_vmx;
}
