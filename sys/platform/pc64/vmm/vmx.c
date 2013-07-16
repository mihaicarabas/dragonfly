#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/vmm_guest_ctl.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>

#include "vmm.h"

#include "vmx.h"
#include "vmx_instr.h"
#include "vmx_vmcs.h"

#define ERROR_ON(func)					\
	do {						\
	if ((err = func)) {				\
		kprintf("VMM: %s error at line: %d\n",	\
		   __func__, __LINE__);			\
		goto error;				\
	}						\
	} while(0)					\

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

/* Declared in generic vmm.c - SYSCTL parent */
extern struct sysctl_oid *vmm_sysctl_tree;

/* SYSCTL tree and context */
static struct sysctl_oid *vmx_sysctl_tree;
static struct sysctl_ctx_list vmx_sysctl_ctx;

/* Per cpu info */
struct vmx_pcpu_info *pcpu_info;

/* VMX BASIC INFO */
uint32_t vmx_revision;
uint32_t vmx_region_size;
uint8_t vmx_width_addr;

/* VMX fixed bits */
uint64_t cr0_fixed_to_0;
uint64_t cr4_fixed_to_0;
uint64_t cr0_fixed_to_1;
uint64_t cr4_fixed_to_1;

/* VMX status */
static uint8_t vmx_enabled = 0;
static uint8_t vmx_initialized = 0;

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
					vmx_ctl->ctls &= ~BIT(bit_no);
				else if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
					vmx_ctl->ctls |= BIT(bit_no);

			} else if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls &= ~BIT(bit_no);

			} else if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls |= BIT(bit_no);

			} else {
				return (EINVAL);
			}
			break;
		case ZERO:
			/* For b.ii) or c.ii) */
			if (!IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls &= ~BIT(bit_no);

			break;
		case ONE:
			/* For b.ii) or c.ii) */
			if (!IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls |= BIT(bit_no);

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
	int cpu;
	pcpu_info = kmalloc(ncpus * sizeof(struct vmx_pcpu_info), M_TEMP, M_WAITOK | M_ZERO);

	for (cpu = 0; cpu < ncpus; cpu++) {

		/* The address must be aligned to 4K - alloc extra */
		pcpu_info[cpu].vmxon_region_na = kmalloc(vmx_region_size + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

		/* Align address */
		pcpu_info[cpu].vmxon_region = (unsigned char*) VMXON_REGION_ALIGN(pcpu_info[cpu].vmxon_region_na);

		/* In the first 31 bits put the vmx revision*/
		*((uint32_t *) pcpu_info[cpu].vmxon_region) = vmx_revision;
	}
}

static void
free_vmxon_regions(void)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		pcpu_info[i].vmxon_region = NULL;

		kfree(pcpu_info[i].vmxon_region_na, M_TEMP);
	}

	kfree(pcpu_info, M_TEMP);
}

static void
build_vmx_sysctl(void)
{
	sysctl_ctx_init(&vmx_sysctl_ctx);
	vmx_sysctl_tree = SYSCTL_ADD_NODE(&vmx_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "vmx",
		    CTLFLAG_RD, 0, "VMX options");

	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "revision", CTLFLAG_RD,
	    &vmx_revision, 0,
	    "VMX revision");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "region_size", CTLFLAG_RD,
	    &vmx_region_size, 0,
	    "VMX region size");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "width_addr", CTLFLAG_RD,
	    &vmx_width_addr, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "pinbased_ctls", CTLFLAG_RD,
	    &vmx_pinbased.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "procbased_ctls", CTLFLAG_RD,
	    &vmx_procbased.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "procbased2_ctls", CTLFLAG_RD,
	    &vmx_procbased2.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "vmexit_ctls", CTLFLAG_RD,
	    &vmx_exit.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "vmentry_ctls", CTLFLAG_RD,
	    &vmx_entry.ctls, 0,
	    "VMX width address");
}

static int
vmx_init(void)
{
	uint64_t feature_control;
	uint64_t vmx_basic_value;
	uint64_t cr0_fixed_bits_to_1;
	uint64_t cr0_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_1;

	int err;


	/*
	 * The ability of a processor to support VMX operation
	 * and related instructions is indicated by:
	 * CPUID.1:ECX.VMX[bit 5] = 1
	 */
	if (!(cpu_feature2 & CPUID2_VMX)) {
		kprintf("VMM: VMX is not supported by this Intel CPU\n");
		return (ENODEV);
	}

	vmx_set_default_settings(&vmx_pinbased);
	vmx_set_default_settings(&vmx_procbased);
	vmx_set_default_settings(&vmx_procbased2);
	vmx_set_default_settings(&vmx_exit);
	vmx_set_default_settings(&vmx_entry);

	/* Enable second level for procbased */
	err = vmx_set_ctl_setting(&vmx_procbased,
	    PROCBASED_ACTIVATE_SECONDARY_CONTROLS,
	    ONE);
	if (err) {
		kprintf("VMM: PROCBASED_ACTIVATE_SECONDARY_CONTROLS not supported by this CPU\n");
		return (ENODEV);
	}

	/* Set 64bits mode for GUEST */
	err = vmx_set_ctl_setting(&vmx_entry,
	    VMENTRY_IA32e_MODE_GUEST,
	    ONE);
	if (err) {
		kprintf("VMM: VMENTRY_IA32e_MODE_GUEST not supported by this CPU\n");
		return (ENODEV);
	}


	/* Set 64bits mode */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_HOST_ADDRESS_SPACE_SIZE,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_HOST_ADDRESS_SPACE_SIZE not supported by this CPU\n");
		return (ENODEV);
	}

//	/* Enable EPT feature */
//	err = vmx_set_ctl_setting(&vmx_procbased2,
//	    PROCBASED2_ENABLE_EPT,
//	    ONE);
//	if (err) {
//		kprintf("VMM: PROCBASED2_ENABLE_EPT not supported by this CPU\n");
//		return (ENODEV);
//	}
//
//	/* Enable VPID feature */
//	err = vmx_set_ctl_setting(&vmx_procbased2,
//	    PROCBASED2_ENABLE_VPID,
//	    ONE);
//	if (err) {
//		kprintf("VMM: PROCBASED2_ENABLE_VPID not supported by this CPU\n");
//		return (ENODEV);
//	}


	/* Check for the feature control status */
	feature_control = rdmsr(IA32_FEATURE_CONTROL);
	if (!(feature_control & BIT(FEATURE_CONTROL_LOCKED))) {
		kprintf("VMM: IA32_FEATURE_CONTROL is not locked\n");
		return (EINVAL);
	}
	if (!(feature_control & BIT(FEATURE_CONTROL_VMX_BIOS_ENABLED))) {
		kprintf("VMM: VMX is disable by the BIOS\n");
		return (EINVAL);
	}

//	wrmsr(IA32_FEATURE_CONTROL, feature_control | BIT(FEATURE_CONTROL_LOCKED) |  BIT(FEATURE_CONTROL_VMX_BIOS_ENABLED));
	vmx_basic_value = rdmsr(IA32_VMX_BASIC);
	vmx_width_addr = (uint8_t) VMX_WIDTH_ADDR(vmx_basic_value);
	vmx_region_size = (uint32_t) VMX_REGION_SIZE(vmx_basic_value);
	vmx_revision = (uint32_t) VMX_REVISION(vmx_basic_value);

	/* A.7 VMX-FIXED BITS IN CR0 */
	cr0_fixed_bits_to_1 = rdmsr(IA32_VMX_CR0_FIXED0);
	cr0_fixed_bits_to_0 = rdmsr(IA32_VMX_CR0_FIXED1);
	cr0_fixed_to_1 = cr0_fixed_bits_to_1 & cr0_fixed_bits_to_0;
	cr0_fixed_to_0 = ~cr0_fixed_bits_to_1 & ~cr0_fixed_bits_to_0;

	/* A.8 VMX-FIXED BITS IN CR4 */
	cr4_fixed_bits_to_1 = rdmsr(IA32_VMX_CR4_FIXED0);
	cr4_fixed_bits_to_0 = rdmsr(IA32_VMX_CR4_FIXED1);
	cr4_fixed_to_1 = cr4_fixed_bits_to_1 & cr4_fixed_bits_to_0;
	cr4_fixed_to_0 = ~cr4_fixed_bits_to_1 & ~cr4_fixed_bits_to_0;

	build_vmx_sysctl();

	vmx_initialized = 1;
	return 0;
}

static void
execute_vmxon(void *perr)
{
	unsigned char *vmxon_region;
	int *err = (int*) perr;

	/* A.7 VMX-FIXED BITS IN CR0 */
	load_cr0((rcr0() | cr0_fixed_to_1) & ~cr0_fixed_to_0);

	/* A.8 VMX-FIXED BITS IN CR4 */
	load_cr4((rcr4() | cr4_fixed_to_1) & ~cr4_fixed_to_0);

	/* Enable VMX */
	load_cr4(rcr4() | CR4_VMXE);

	vmxon_region = pcpu_info[mycpuid].vmxon_region;
	*err = vmxon(vmxon_region);
	if (*err) {
		kprintf("VMM: vmxon failed on cpu%d\n", mycpuid);
	}
}

static void
execute_vmxoff(void *dummy)
{
	vmxoff();

	/* Disable VMX */
	load_cr4(rcr4() & ~CR4_VMXE);
}

static void
execute_vmclear(void *vmcs_region)
{
	int err;
	char *vmcs_reg = (char*) vmcs_region;

	err = vmclear(vmcs_reg);
	if (err) {
		kprintf("VMM: vmclear failed on cpu%d\n", mycpuid);
	}

}

static int
vmx_enable(void)
{
	int err;
	globaldata_t gd;
	int cpu, seq;

	if (!vmx_initialized) {
		kprintf("VMM: vmx_enable - not allowed; vmx not initialized\n");
		return (EINVAL);
	}

	if (vmx_enabled) {
		kprintf("VMM: vmx_enable - already enabled\n");
		return (EINVAL);
	}

	alloc_vmxon_regions();

	for (cpu = 0; cpu < ncpus; cpu++) {
		err = 0;
		gd = globaldata_find(cpu);
		seq = lwkt_send_ipiq(gd, execute_vmxon, &err);
		lwkt_wait_ipiq(gd, seq);
		if(err) {
			kprintf("VMM: vmx_enable error %d on cpu%d\n", err, cpu);
			return err;
		}
	}
	vmx_enabled = 1;
	return 0;
}

static int
vmx_disable(void)
{
	globaldata_t gd;
	int cpu, seq;

	if (!vmx_enabled) {
		kprintf("VMM: vmx_disable not allowed; vmx wasn't enabled\n");
	}

	for (cpu = 0; cpu < ncpus; cpu++) {
		gd = globaldata_find(cpu);
		seq = lwkt_send_ipiq(gd, execute_vmxoff, NULL);
		lwkt_wait_ipiq(gd, seq);
	}

	free_vmxon_regions();

	vmx_enabled = 0;

	return 0;
}

static int
vmx_vminit(struct guest_options *options)
{
	struct vmx_thread_info * vti;
	int err;
	struct globaldata *gd = mycpu;
	struct pcb *curpcb = curthread->td_pcb;

	vti = kmalloc(sizeof(struct vmx_thread_info), M_TEMP, M_WAITOK | M_ZERO);
	vti->vmcs_region_na = kmalloc(vmx_region_size + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

	/* Align address */
	vti->vmcs_region = (unsigned char*) VMXON_REGION_ALIGN(vti->vmcs_region_na);

	/* In the first 31 bits put the vmx revision*/
	*((uint32_t *)vti->vmcs_region) = vmx_revision;

	/* vmclear the vmcs to initialize it */
	ERROR_ON(vmclear(vti->vmcs_region));

	/* Make this the current VMCS */
	ERROR_ON(vmptrld(vti->vmcs_region));

	/* Load the VMX controls */
	ERROR_ON(vmwrite(VMCS_PINBASED_CTLS, vmx_pinbased.ctls));
	ERROR_ON(vmwrite(VMCS_PROCBASED_CTLS, vmx_procbased.ctls));
	ERROR_ON(vmwrite(VMCS_PROCBASED2_CTLS, vmx_procbased2.ctls));
	ERROR_ON(vmwrite(VMCS_VMEXIT_CTLS, vmx_exit.ctls));
	ERROR_ON(vmwrite(VMCS_VMENTRY_CTLS, vmx_entry.ctls));

	/* Load HOST CRs */
	ERROR_ON(vmwrite(VMCS_HOST_CR0, rcr0()));
	ERROR_ON(vmwrite(VMCS_HOST_CR4, rcr4()));

	/* Load HOST EFER and PAT */
	ERROR_ON(vmwrite(VMCS_HOST_IA32_PAT, rdmsr(MSR_PAT)));
	ERROR_ON(vmwrite(VMCS_HOST_IA32_EFER, rdmsr(MSR_EFER)));
	
	/* Load HOST selectors */
	ERROR_ON(vmwrite(VMCS_HOST_ES_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_ON(vmwrite(VMCS_HOST_SS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_ON(vmwrite(VMCS_HOST_FS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_ON(vmwrite(VMCS_HOST_GS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_ON(vmwrite(VMCS_HOST_CS_SELECTOR, GSEL(GCODE_SEL, SEL_KPL)));
	ERROR_ON(vmwrite(VMCS_HOST_TR_SELECTOR, GSEL(GPROC0_SEL, SEL_KPL)));

	/* Load Host FS base (not used) - 0 */
	ERROR_ON(vmwrite(VMCS_HOST_FS_BASE, 0));

	/*
	 * The next BASE addresses are written on each VMRUN in case
	 * the CPU changes because are per-CPU values
	 */
	ERROR_ON(vmwrite(VMCS_HOST_CR3, rcr3()));

	ERROR_ON(vmwrite(VMCS_HOST_GS_BASE, (uint64_t) gd)); /* mycpu points to %gs:0 */
	ERROR_ON(vmwrite(VMCS_HOST_TR_BASE, (uint64_t) &gd->gd_prvspace->mdglobaldata.gd_common_tss));

	ERROR_ON(vmwrite(VMCS_HOST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
	ERROR_ON(vmwrite(VMCS_HOST_IDTR_BASE, (uint64_t) r_idt_arr[gd->gd_cpuid].rd_base));

	/*
	 * Call vmx_vmexit on VM_EXIT condition
	 * The RSP will point to the vmx_thread_info
	 */
	ERROR_ON(vmwrite(VMCS_HOST_RIP, (uint64_t) vmx_vmexit));
	ERROR_ON(vmwrite(VMCS_HOST_RSP, (uint64_t) vti));

	/*
	 * GUEST initialization
	 */

	ERROR_ON(vmwrite(VMCS_GUEST_ES_SELECTOR, GSEL(GUDATA_SEL, SEL_UPL)));
	ERROR_ON(vmwrite(VMCS_GUEST_SS_SELECTOR, GSEL(GUDATA_SEL, SEL_UPL)));
	ERROR_ON(vmwrite(VMCS_GUEST_FS_SELECTOR, GSEL(GUDATA_SEL, SEL_UPL)));
	ERROR_ON(vmwrite(VMCS_GUEST_GS_SELECTOR, GSEL(GUDATA_SEL, SEL_UPL)));
	ERROR_ON(vmwrite(VMCS_GUEST_CS_SELECTOR, GSEL(GUCODE_SEL, SEL_UPL)));
	ERROR_ON(vmwrite(VMCS_GUEST_TR_SELECTOR, GSEL(GPROC0_SEL, SEL_UPL))); /* TODO */

	ERROR_ON(vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P));
	ERROR_ON(vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, VMCS_SEG_TYPE(11) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P | VMCS_L));
	ERROR_ON(vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P));
	ERROR_ON(vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P));
	ERROR_ON(vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P));
	ERROR_ON(vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P));
	ERROR_ON(vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, VMCS_SEG_UNUSABLE));
	ERROR_ON(vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, VMCS_SEG_TYPE(11) | VMCS_P));

	ERROR_ON(vmwrite(VMCS_GUEST_CR0, (CR0_PE | CR0_PG | cr0_fixed_to_1) & ~cr0_fixed_to_0));
	ERROR_ON(vmwrite(VMCS_GUEST_CR4, (CR4_PAE | cr4_fixed_to_1) & ~ cr4_fixed_to_0));

	ERROR_ON(vmwrite(VMCS_GUEST_RFLAGS, 0x02));

	ERROR_ON(vmwrite(VMCS_GUEST_CR3, (uint64_t) curpcb->pcb_cr3));

	ERROR_ON(vmwrite(VMCS_GUEST_GS_BASE, (uint64_t) curpcb->pcb_gsbase)); /* mycpu points to %gs:0 */
	ERROR_ON(vmwrite(VMCS_GUEST_FS_BASE, (uint64_t) curpcb->pcb_fsbase));

	ERROR_ON(vmwrite(VMCS_GUEST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
	ERROR_ON(vmwrite(VMCS_GUEST_IDTR_BASE, (uint64_t) r_idt_arr[gd->gd_cpuid].rd_base));

	ERROR_ON(vmwrite(VMCS_GUEST_RIP, options->ip));
	ERROR_ON(vmwrite(VMCS_GUEST_RSP, options->sp));


	ERROR_ON(vmwrite(VMCS_GUEST_IA32_SYSENTER_ESP, rdmsr(MSR_SYSENTER_ESP_MSR)));
	ERROR_ON(vmwrite(VMCS_GUEST_IA32_SYSENTER_EIP, rdmsr(MSR_SYSENTER_EIP_MSR)));

	ERROR_ON(vmwrite(VMCS_HOST_IA32_SYSENTER_EIP, rdmsr(MSR_SYSENTER_EIP_MSR)));
	ERROR_ON(vmwrite(VMCS_HOST_IA32_SYSENTER_ESP, rdmsr(MSR_SYSENTER_ESP_MSR)));
	ERROR_ON(vmwrite(VMCS_HOST_IA32_SYSENTER_CS, rdmsr(MSR_SYSENTER_CS_MSR)));



	/*
	 * This field is included for future expansion.
	 * Software should set this field to FFFFFFFF_FFFFFFFFH
	 * to avoid VM-entry failures (see Section 26.3.1.5).
	 */
	ERROR_ON(vmwrite(VMCS_LINK_POINTER, ~0ULL));

	/* Initialized on this CPU */
	vti->launched = 0;
	vti->last_cpu = gd->gd_cpuid;
	pcpu_info[gd->gd_cpuid].loaded_vmcs = vti->vmcs_region;

	curthread->td_vmm = (void*) vti;

	return 0;
error:
	kprintf("VMM: vmx_vminit failed\n");

	kfree(vti->vmcs_region_na, M_TEMP);
	kfree(vti, M_TEMP);
	return err;
}

static int
vmx_vmdestroy(void)
{
	struct vmx_thread_info * vti = (struct vmx_thread_info *) curthread->td_vmm;

	if (vti != NULL) {
		if (vti->vmcs_region_na != NULL) {
			kfree(vti->vmcs_region_na, M_TEMP);
			kfree(vti, M_TEMP);

			curthread->td_vmm = NULL;

			return 0;
		}
	}
	return -1;
}

static int
handle_vmx_vmexit(void)
{
	uint64_t val;
	int exit_reason;
	int err;

	ERROR_ON(vmread(VMCS_VMEXIT_REASON, &val));

	exit_reason = VMCS_BASIC_EXIT_REASON(val);

	switch (exit_reason) {
		case EXIT_REASON_EXCEPTION:
			kprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXCEPTION\n");
			err = -1;
			goto error;
			break;
		case EXIT_REASON_EXT_INTR:
			kprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXT_INTR\n");
			err = -1;
			goto error;
			break;
		default:
			ERROR_ON(vmread(VMCS_EXIT_QUALIFICATION, &val));
			kprintf("VMM: handle_vmx_vmexit: unknown exit reason: %d with qualification %lld\n", exit_reason, (long long) val);
			err = -1;
			goto error;
			break;
	}
	return 0;
error:
	return err;
}

static int
vmx_vmrun(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	struct globaldata *last_gd;
	int seq;
	int err;
	int ret;
	uint64_t val;
restart:
	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;

	if(vti == NULL ){
		kprintf("VMM: vmx_vmrum vti is NULL\n");
		return -1;
	}

	if (vti->last_cpu != gd->gd_cpuid) {
		kprintf("VMM: vmx_vmrun CPU changed\n");
		/* Clear the VMCS area if ran on another CPU */
		last_gd = globaldata_find(vti->last_cpu);
		seq = lwkt_send_ipiq(last_gd, execute_vmclear, vti->vmcs_region);
		lwkt_wait_ipiq(gd, seq);

		ERROR_ON(vmptrld(vti->vmcs_region));
		pcpu_info[gd->gd_cpuid].loaded_vmcs = vti->vmcs_region;

		ERROR_ON(vmwrite(VMCS_HOST_CR3, rcr3()));

		ERROR_ON(vmwrite(VMCS_HOST_GS_BASE, (uint64_t) gd)); /* mycpu points to %gs:0 */
		ERROR_ON(vmwrite(VMCS_HOST_TR_BASE, (uint64_t) &gd->gd_prvspace->mdglobaldata.gd_common_tss));

		ERROR_ON(vmwrite(VMCS_HOST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
		ERROR_ON(vmwrite(VMCS_HOST_IDTR_BASE, (uint64_t) r_idt_arr[gd->gd_cpuid].rd_base));

		vti->launched = 0;
		vti->last_cpu = gd->gd_cpuid;

	} else if (pcpu_info[gd->gd_cpuid].loaded_vmcs != vti->vmcs_region) {
		kprintf("VMM: vmx_vmrun vmcs is not loaded\n");

		/* If another VMCS was loaded, reload this one */
		ERROR_ON(vmptrld(vti->vmcs_region));
		pcpu_info[gd->gd_cpuid].loaded_vmcs = vti->vmcs_region;

		vti->launched = 0;
	}

	if (vti->launched) { /* vmresume */
		kprintf("VMM: vmx_vmrun: vmx_resume\n");
		ret = vmx_resume(vti);

	} else { /* vmlaunch */
		kprintf("VMM: vmx_vmrun: vmx_launch\n");
		vti->launched = 1;
		ret = vmx_launch(vti);
	}

	if (ret == VM_EXIT) {
		kprintf("VMM: vmx_vmrun: VM_EXIT issued\n");
		if (handle_vmx_vmexit())
			goto done;
		/* We handled the VMEXIT reason and continue with VM execution */
		goto restart;

	} else {
		vti->launched = 0;
		if (ret == VM_FAIL_VALID) {
			vmread(VMCS_INSTR_ERR, &val);
			err = (int) val;
			kprintf("VMM: vmx_vmrun: vmenter failed with VM_FAIL_VALID, error code %d\n", err);
		} else {
			kprintf("VMM: vmx_vmrun: vmenter failed with VM_FAIL_INVALID\n");
		}
		goto error;
	}
done:
	kprintf("VMM: vmx_vmrun: returning with success\n");
	return 0;

error:
	kprintf("VMM: vmx_vmrun failed\n");
	return err;
}


static struct vmm_ctl ctl_vmx = {
	.name = "VMX from Intel",
	.init = vmx_init,
	.enable = vmx_enable,
	.disable = vmx_disable,
	.vminit = vmx_vminit,
	.vmdestroy = vmx_vmdestroy,
	.vmrun = vmx_vmrun,
};

struct vmm_ctl*
get_ctl_intel(void){
	return &ctl_vmx;
}
