#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/vmm_guest_ctl.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/vkernel.h>
#include <ddb/ddb.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/trap.h>
#include <machine/pmap.h>

#include <vm/vm_map.h>

#include "vmm.h"
#include "vmm_utils.h"

#include "vmx.h"
#include "vmx_instr.h"
#include "vmx_vmcs.h"

extern void trap(struct trapframe *frame);
extern void syscall2(struct trapframe *frame);

static int vmx_check_cpu_migration(void);
static int execute_vmptrld(struct vmx_thread_info *vti);

struct instr_decode syscall_asm = {
	.opcode_bytes = 2,
	.opcode.byte1 = 0x0F,
	.opcode.byte2 = 0x05,
};

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

/* IA32_VMX_EPT_VPID_CAP */
uint64_t vmx_ept_vpid_cap;

/* VMX fixed bits */
uint64_t cr0_fixed_to_0;
uint64_t cr4_fixed_to_0;
uint64_t cr0_fixed_to_1;
uint64_t cr4_fixed_to_1;

/* VMX status */
static uint8_t vmx_enabled = 0;
static uint8_t vmx_initialized = 0;

static uint64_t pmap_bits_ept[PG_BITS_SIZE];
static pt_entry_t pmap_cache_bits_ept[PAT_INDEX_SIZE];
static int ept_protection_codes[PROTECTION_CODES_SIZE];
static pt_entry_t pmap_cache_mask_ept;

static int pmap_pm_flags_ept;

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
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "ept_vpid_cap", CTLFLAG_RD,
	    &vmx_ept_vpid_cap, 0,
	    "VMX EPT VPID CAP");
}

static int
vmx_ept_init(void){
	int prot;
	/* Chapter 28 VMX SUPPORT FOR ADDRESS TRANSLATION
	 * Intel Manual 3c, page 107
	 */

	if(!EPT_PWL4(vmx_ept_vpid_cap)) {
		return EINVAL;
	}

	pmap_bits_ept[TYPE_IDX] = EPT_PMAP;
	pmap_bits_ept[PG_V_IDX] = EPT_PG_READ | EPT_PG_EXECUTE;
	pmap_bits_ept[PG_RW_IDX] = EPT_PG_READ | EPT_PG_EXECUTE | EPT_PG_WRITE;
	pmap_bits_ept[PG_PS_IDX] = EPT_PG_PS;
	pmap_bits_ept[PG_G_IDX] = 0;
	pmap_bits_ept[PG_W_IDX] = EPT_PG_AVAIL1;
	pmap_bits_ept[PG_MANAGED_IDX] = EPT_PG_AVAIL2;
	pmap_bits_ept[PG_DEVICE_IDX] = EPT_PG_AVAIL3;
	pmap_bits_ept[PG_N_IDX] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;

	if (EPT_AD_BITS_SUPPORTED(vmx_ept_vpid_cap)) {
		pmap_bits_ept[PG_A_IDX] = 1ULL << 8;
		pmap_bits_ept[PG_M_IDX] = 1ULL << 8;
	} else {
		pmap_bits_ept[PG_A_IDX] = 0;
		pmap_bits_ept[PG_M_IDX] = 0;
		pmap_pm_flags_ept = PMAP_EMULATE_AD_BITS;
	}

	pmap_cache_mask_ept = EPT_IGNORE_PAT | EPT_MEM_TYPE_MASK;

	pmap_cache_bits_ept[PAT_UNCACHEABLE] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;
	pmap_cache_bits_ept[PAT_WRITE_COMBINING] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WC;
	pmap_cache_bits_ept[PAT_WRITE_THROUGH] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WT;
	pmap_cache_bits_ept[PAT_WRITE_PROTECTED] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WP;
	pmap_cache_bits_ept[PAT_WRITE_BACK] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WB;
	pmap_cache_bits_ept[PAT_UNCACHED] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;

	for (prot = 0; prot < PROTECTION_CODES_SIZE; prot++) {
		switch (prot) {
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_EXECUTE:
			ept_protection_codes[prot] = 0;
			break;
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
			ept_protection_codes[prot] = pmap_bits_ept[PG_RW_IDX];

			break;
		}
	}

	return 0;
}

static void
regular2ept_pmap(void)
{
	struct vmx_thread_info * vti = (struct vmx_thread_info *) curthread->td_vmm;
	pmap_t pmap = &curthread->td_lwp->lwp_vmspace->vm_pmap;

	pmap->pm_flags |= pmap_pm_flags_ept;

	bcopy(pmap->pmap_bits, vti->pmap_bits, sizeof(pmap->pmap_bits));
	bcopy(pmap->protection_codes, vti->protection_codes, sizeof(pmap->protection_codes));
	bcopy(pmap->pmap_cache_bits, vti->pmap_cache_bits, sizeof(pmap->pmap_cache_bits));
	vti->pmap_cache_mask = pmap->pmap_cache_mask;

	bcopy(pmap_bits_ept, pmap->pmap_bits, sizeof(pmap_bits_ept));
	bcopy(ept_protection_codes, pmap->protection_codes, sizeof(ept_protection_codes));
	bcopy(pmap_cache_bits_ept, pmap->pmap_cache_bits, sizeof(pmap_cache_bits_ept));
	pmap->pmap_cache_mask = pmap_cache_mask_ept;
}

static void
ept2regular_pmap(void)
{
	struct vmx_thread_info * vti = (struct vmx_thread_info *) curthread->td_vmm;
	pmap_t pmap = &curthread->td_lwp->lwp_vmspace->vm_pmap;

	pmap->pm_flags &= ~pmap_pm_flags_ept;

	bcopy(vti->pmap_bits, pmap->pmap_bits, sizeof(pmap->pmap_bits));
	bcopy(vti->protection_codes, pmap->protection_codes, sizeof(pmap->protection_codes));
	bcopy(vti->pmap_cache_bits, pmap->pmap_cache_bits, sizeof(pmap->pmap_cache_bits));
	pmap->pmap_cache_mask = vti->pmap_cache_mask;
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

	/* Enable external interrupts exiting */
	err = vmx_set_ctl_setting(&vmx_pinbased,
	    PINBASED_EXTERNAL_INTERRUPT_EXITING,
	    ONE);
	if (err) {
		kprintf("VMM: PINBASED_EXTERNAL_INTERRUPT_EXITING not supported by this CPU\n");
		return (ENODEV);
	}

	/* Enable non-maskable interrupts exiting */
	err = vmx_set_ctl_setting(&vmx_pinbased,
	    PINBASED_NMI_EXITING,
	    ONE);
	if (err) {
		kprintf("VMM: PINBASED_NMI_EXITING not supported by this CPU\n");
		return (ENODEV);
	}

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

	/* Load MSR EFER on enry */
	err = vmx_set_ctl_setting(&vmx_entry,
	    VMENTRY_LOAD_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMENTRY_LOAD_IA32_EFER not supported by this CPU\n");
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

	/* Save/Load Efer on exit */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_SAVE_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_SAVE_IA32_EFER not supported by this CPU\n");
		return (ENODEV);
	}

	/* Load Efer on exit */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_LOAD_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_LOAD_IA32_EFER not supported by this CPU\n");
		return (ENODEV);
	}
	
	/* Enable EPT feature */
	err = vmx_set_ctl_setting(&vmx_procbased2,
	    PROCBASED2_ENABLE_EPT,
	    ONE);
	if (err) {
		kprintf("VMM: PROCBASED2_ENABLE_EPT not supported by this CPU\n");
		return (ENODEV);
	}

	vmx_ept_vpid_cap = rdmsr(IA32_VMX_EPT_VPID_CAP);
	if (vmx_ept_init()) {
		kprintf("VMM: vmx_ept_init failes\n");
		return (ENODEV);
	}

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
execute_vmclear(void *data)
{
	struct vmx_thread_info *vti = data;
	int err;
	globaldata_t gd = mycpu;

	if (pcpu_info[gd->gd_cpuid].loaded_vmx == vti) {
		/*
		 * Must set vti->launched to zero after vmclear'ing to
		 * force a vmlaunch the next time.
		 */
		pcpu_info[gd->gd_cpuid].loaded_vmx = NULL;
		vti->launched = 0;
		ERROR_IF(vmclear(vti->vmcs_region));
	}
error:
	return;
}

static int
execute_vmptrld(struct vmx_thread_info *vti)
{
	globaldata_t gd = mycpu;

	/*
	 * Must vmclear previous active vcms if it is different.
	 */
	if (pcpu_info[gd->gd_cpuid].loaded_vmx &&
	    pcpu_info[gd->gd_cpuid].loaded_vmx != vti)
		execute_vmclear(pcpu_info[gd->gd_cpuid].loaded_vmx);

	/*
	 * Make this the current VMCS.  Must set loaded_vmx field
	 * before calling vmptrld() to avoid races against cpusync.
	 *
	 * Must set vti->launched to zero after the vmptrld to force
	 * a vmlaunch.
	 */
	if (pcpu_info[gd->gd_cpuid].loaded_vmx != vti) {
		vti->launched = 0;
		pcpu_info[gd->gd_cpuid].loaded_vmx = vti;
		return (vmptrld(vti->vmcs_region));
	} else {
		return (0);
	}
}

static int
vmx_enable(void)
{
	int err;
	int cpu;

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
		lwkt_cpusync_simple(CPUMASK(cpu), execute_vmxon, &err);
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
	int cpu;

	if (!vmx_enabled) {
		kprintf("VMM: vmx_disable not allowed; vmx wasn't enabled\n");
	}

	for (cpu = 0; cpu < ncpus; cpu++)
		lwkt_cpusync_simple(CPUMASK(cpu), execute_vmxoff, NULL);

	free_vmxon_regions();

	vmx_enabled = 0;

	return 0;
}
static int vmx_set_guest_descriptor(descriptor_t type,
		uint16_t selector,
		uint32_t rights,
		uint64_t base,
		uint32_t limit)
{
	int err;
	int selector_enc;
	int rights_enc;
	int base_enc;
	int limit_enc;


	/*
	 * Intel Manual Vol 3C. - page 60
	 * If any bit in the limit field in the range 11:0 is 0, G must be 0.
	 * If any bit in the limit field in the range 31:20 is 1, G must be 1.
	 */
	if ((~rights & VMCS_SEG_UNUSABLE) || (type == CS)) {
		if ((limit & 0xfff) != 0xfff)
			rights &= ~VMCS_G;
		else if ((limit & 0xfff00000) != 0)
			rights |= VMCS_G;
	}

	switch(type) {
		case ES:
			selector_enc = VMCS_GUEST_ES_SELECTOR;
			rights_enc = VMCS_GUEST_ES_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_ES_BASE;
			limit_enc = VMCS_GUEST_ES_LIMIT;
			break;
		case CS:
			selector_enc = VMCS_GUEST_CS_SELECTOR;
			rights_enc = VMCS_GUEST_CS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_CS_BASE;
			limit_enc = VMCS_GUEST_CS_LIMIT;
			break;
		case SS:
			selector_enc = VMCS_GUEST_SS_SELECTOR;
			rights_enc = VMCS_GUEST_SS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_SS_BASE;
			limit_enc = VMCS_GUEST_SS_LIMIT;
			break;
		case DS:
			selector_enc = VMCS_GUEST_DS_SELECTOR;
			rights_enc = VMCS_GUEST_DS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_DS_BASE;
			limit_enc = VMCS_GUEST_DS_LIMIT;
			break;
		case FS:
			selector_enc = VMCS_GUEST_FS_SELECTOR;
			rights_enc = VMCS_GUEST_FS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_FS_BASE;
			limit_enc = VMCS_GUEST_FS_LIMIT;
			break;
		case GS:
			selector_enc = VMCS_GUEST_GS_SELECTOR;
			rights_enc = VMCS_GUEST_GS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_GS_BASE;
			limit_enc = VMCS_GUEST_GS_LIMIT;
			break;
		case LDTR:
			selector_enc = VMCS_GUEST_LDTR_SELECTOR;
			rights_enc = VMCS_GUEST_LDTR_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_LDTR_BASE;
			limit_enc = VMCS_GUEST_LDTR_LIMIT;
			break;
		case TR:
			selector_enc = VMCS_GUEST_TR_SELECTOR;
			rights_enc = VMCS_GUEST_TR_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_TR_BASE;
			limit_enc = VMCS_GUEST_TR_LIMIT;
			break;
		default:
			kprintf("VMM: vmx_set_guest_descriptor: unknown descripton\n");
			err = -1;
			goto error;
			break;
	}

	ERROR_IF(vmwrite(selector_enc, selector));
	ERROR_IF(vmwrite(rights_enc, rights));
	ERROR_IF(vmwrite(base_enc, base));
	ERROR_IF(vmwrite(limit_enc, limit));

	return 0;
error:
	kprintf("VMM: vmx_set_guest_descriptor failed\n");
	return err;
}

static int
vmx_vminit(struct guest_options *options)
{
	struct vmx_thread_info * vti;
	int err;
	struct tls_info guest_fs = curthread->td_tls.info[0];
	struct tls_info guest_gs = curthread->td_tls.info[1];
	struct pcb *curpcb = curthread->td_pcb;


	vti = kmalloc(sizeof(struct vmx_thread_info), M_TEMP, M_WAITOK | M_ZERO);
	vti->vmcs_region_na = kmalloc(vmx_region_size + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

	/* Align address */
	vti->vmcs_region = (unsigned char*) VMXON_REGION_ALIGN(vti->vmcs_region_na);
	vti->last_cpu = -1;

	/* In the first 31 bits put the vmx revision*/
	*((uint32_t *)vti->vmcs_region) = vmx_revision;

	/*
	 * vmclear the vmcs to initialize it.
	 */
	ERROR_IF(vmclear(vti->vmcs_region));

	crit_enter();

	ERROR_IF(execute_vmptrld(vti));

	/* Load the VMX controls */
	ERROR_IF(vmwrite(VMCS_PINBASED_CTLS, vmx_pinbased.ctls));
	ERROR_IF(vmwrite(VMCS_PROCBASED_CTLS, vmx_procbased.ctls));
	ERROR_IF(vmwrite(VMCS_PROCBASED2_CTLS, vmx_procbased2.ctls));
	ERROR_IF(vmwrite(VMCS_VMEXIT_CTLS, vmx_exit.ctls));
	ERROR_IF(vmwrite(VMCS_VMENTRY_CTLS, vmx_entry.ctls));

	/* Load HOST CRs */
	ERROR_IF(vmwrite(VMCS_HOST_CR0, rcr0()));
	ERROR_IF(vmwrite(VMCS_HOST_CR4, rcr4()));

	/* Load HOST EFER and PAT */
//	ERROR_IF(vmwrite(VMCS_HOST_IA32_PAT, rdmsr(MSR_PAT)));
	ERROR_IF(vmwrite(VMCS_HOST_IA32_EFER, rdmsr(MSR_EFER)));

	/* Load HOST selectors */
	ERROR_IF(vmwrite(VMCS_HOST_ES_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_SS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_FS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_GS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_CS_SELECTOR, GSEL(GCODE_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_TR_SELECTOR, GSEL(GPROC0_SEL, SEL_KPL)));

	/*
	 * The BASE addresses are written on each VMRUN in case
	 * the CPU changes because are per-CPU values
	 */

	/*
	 * Call vmx_vmexit on VM_EXIT condition
	 * The RSP will point to the vmx_thread_info
	 */
	ERROR_IF(vmwrite(VMCS_HOST_RIP, (uint64_t) vmx_vmexit));
	ERROR_IF(vmwrite(VMCS_HOST_RSP, (uint64_t) vti));

	/*
	 * GUEST initialization
	 */
	ERROR_IF(vmx_set_guest_descriptor(ES, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(SS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(DS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(FS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_fs.base, (uint32_t) guest_fs.size));

	ERROR_IF(vmx_set_guest_descriptor(GS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_gs.base, (uint32_t) guest_gs.size));

	ERROR_IF(vmx_set_guest_descriptor(CS, GSEL(GUCODE_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(11) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P | VMCS_L,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(TR, GSEL(GPROC0_SEL, SEL_UPL),
			VMCS_SEG_TYPE(11) | VMCS_P,
			0, 0));

	ERROR_IF(vmx_set_guest_descriptor(LDTR, 0, VMCS_SEG_UNUSABLE, 0, 0));


	ERROR_IF(vmwrite(VMCS_GUEST_CR0, (CR0_PE | CR0_PG | cr0_fixed_to_1) & ~cr0_fixed_to_0));
	ERROR_IF(vmwrite(VMCS_GUEST_CR4, (CR4_PAE | CR4_FXSR | CR4_XMM | CR4_XSAVE | cr4_fixed_to_1) & ~ cr4_fixed_to_0));

	ERROR_IF(vmwrite(VMCS_GUEST_IA32_EFER, (EFER_LME | EFER_LMA)));

	ERROR_IF(vmwrite(VMCS_GUEST_RFLAGS, PSL_I | 0x02));

	ERROR_IF(vmwrite(VMCS_GUEST_CR3, (uint64_t) curpcb->pcb_cr3));

	ERROR_IF(vmwrite(VMCS_EXCEPTION_BITMAP,(uint64_t) 0xFFFFFFFF));

	/* Guest RIP and RSP */
	ERROR_IF(vmwrite(VMCS_GUEST_RIP, options->ip));
	ERROR_IF(vmwrite(VMCS_GUEST_RSP, options->sp));

	/*
	 * This field is included for future expansion.
	 * Software should set this field to FFFFFFFF_FFFFFFFFH
	 * to avoid VM-entry failures (see Section 26.3.1.5).
	 */
	ERROR_IF(vmwrite(VMCS_LINK_POINTER, ~0ULL));

	curthread->td_vmm = (void*) vti;

	crit_exit();

	/* Guest trapframe */
	vti->guest.tf_rip = options->ip;
	vti->guest.tf_cs = GSEL(GUCODE_SEL, SEL_UPL);
	vti->guest.tf_rflags = PSL_I | 0x02;
	vti->guest.tf_rsp = options->sp;
	vti->guest.tf_ss = GSEL(GUDATA_SEL, SEL_UPL);

	return 0;
error:
	crit_exit();

	kprintf("VMM: vmx_vminit failed\n");
	execute_vmclear(vti);

	kfree(vti->vmcs_region_na, M_TEMP);
	kfree(vti, M_TEMP);
	return err;
}

static int
vmx_vmdestroy(void)
{
	struct vmx_thread_info *vti = curthread->td_vmm;
	int error = -1;

	if (vti != NULL) {
		vmx_check_cpu_migration();
		if (vti->vmcs_region &&
		    pcpu_info[mycpu->gd_cpuid].loaded_vmx == vti)
			execute_vmclear(vti);

		if (vti->vmcs_region_na != NULL) {
			kfree(vti->vmcs_region_na, M_TEMP);
			kfree(vti, M_TEMP);
			error = 0;
		}
		curthread->td_vmm = NULL;
	}
	return error;
}

/*
 * Checks if we migrated to another cpu
 *
 * No locks are required
 */
static int
vmx_check_cpu_migration(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	int err;

	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	if (vti->last_cpu != -1 && vti->last_cpu != gd->gd_cpuid &&
	    pcpu_info[vti->last_cpu].loaded_vmx == vti) {
		/*
		 * Do not reset last_cpu to -1 here, leave it caching
		 * the cpu whos per-cpu fields the VMCS is synchronized
		 * with.  The pcpu_info[] check prevents unecessary extra
		 * cpusyncs.
		 */
		dkprintf("VMM: cpusync from %d to %d\n", gd->gd_cpuid, vti->last_cpu);

		/* Clear the VMCS area if ran on another CPU */
		lwkt_cpusync_simple(CPUMASK(vti->last_cpu),
				    execute_vmclear, (void *)vti);
	}
	return 0;
error:
	kprintf("VMM: vmx_check_cpu_migration failed\n");
	return err;
}

/* Handle CPU migration
 *
 * We have to enter with interrupts disabled/critical section
 * to be sure that another VMCS won't steel our CPU.
 */
static inline int
vmx_handle_cpu_migration(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	int err;

	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	if (vti->last_cpu != gd->gd_cpuid) {
		/*
		 * We need to synchronize the per-cpu fields after changing
		 * cpus.
		 */
		dkprintf("VMM: vmx_handle_cpu_migration init per CPU data\n");

		ERROR_IF(execute_vmptrld(vti));

		/* Host related registers */
		ERROR_IF(vmwrite(VMCS_HOST_GS_BASE, (uint64_t) gd)); /* mycpu points to %gs:0 */
		ERROR_IF(vmwrite(VMCS_HOST_TR_BASE, (uint64_t) &gd->gd_prvspace->mdglobaldata.gd_common_tss));

		ERROR_IF(vmwrite(VMCS_HOST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
		ERROR_IF(vmwrite(VMCS_HOST_IDTR_BASE, (uint64_t) r_idt_arr[gd->gd_cpuid].rd_base));


		/* Guest related register */
		ERROR_IF(vmwrite(VMCS_GUEST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
		ERROR_IF(vmwrite(VMCS_GUEST_GDTR_LIMIT, (uint64_t) (NGDT * sizeof(gdt[0]) - 1)));

		/*
		 * Indicates which cpu the per-cpu fields are synchronized
		 * with.  Does not indicate whether the vmcs is active on
		 * that particular cpu.
		 */
		vti->last_cpu = gd->gd_cpuid;
	} else if (pcpu_info[gd->gd_cpuid].loaded_vmx != vti) {
		/*
		 * We only need to vmptrld
		 */
		dkprintf("VMM: vmx_handle_cpu_migration: vmcs is not loaded\n");

		ERROR_IF(execute_vmptrld(vti));

	} /* else we don't need to do anything */
	return 0;
error:
	kprintf("VMM: vmx_handle_cpu_migration failed\n");
	return err;
}

/* Load information about VMexit
 *
 * We still are with interrupts disabled/critical secion
 * because we must operate with the VMCS on the CPU
 */
static inline int
vmx_vmexit_loadinfo(void)
{
	struct vmx_thread_info *vti;
	int err;

	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	ERROR_IF(vmread(VMCS_VMEXIT_REASON, &vti->vmexit_reason));
	ERROR_IF(vmread(VMCS_EXIT_QUALIFICATION, &vti->vmexit_qualification));
	ERROR_IF(vmread(VMCS_VMEXIT_INTERRUPTION_INFO, &vti->vmexit_interruption_info));
	ERROR_IF(vmread(VMCS_VMEXIT_INTERRUPTION_ERROR, &vti->vmexit_interruption_error));
	ERROR_IF(vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH, &vti->vmexit_instruction_length));

	ERROR_IF(vmread(VMCS_GUEST_RIP, &vti->guest.tf_rip));
	ERROR_IF(vmread(VMCS_GUEST_CS_SELECTOR, &vti->guest.tf_cs));
	ERROR_IF(vmread(VMCS_GUEST_RFLAGS, &vti->guest.tf_rflags));
	ERROR_IF(vmread(VMCS_GUEST_RSP, &vti->guest.tf_rsp));
	ERROR_IF(vmread(VMCS_GUEST_SS_SELECTOR, &vti->guest.tf_ss));

	return 0;
error:
	kprintf("VMM: vmx_vmexit_loadinfo failed\n");
	return err;
}


static int
vmx_set_tls_area(void)
{
	struct tls_info *guest_fs = &curthread->td_tls.info[0];
	struct tls_info *guest_gs = &curthread->td_tls.info[1];

	int err;

	dkprintf("VMM: vmx_set_tls_area hook\n");

	crit_enter();

	ERROR_IF(vmx_check_cpu_migration());
	ERROR_IF(vmx_handle_cpu_migration());

	/* set %fs */
	ERROR_IF(vmx_set_guest_descriptor(FS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_fs->base, (uint32_t) guest_fs->size));
	
	/* set %gs */
	ERROR_IF(vmx_set_guest_descriptor(GS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_gs->base, (uint32_t) guest_gs->size));

	crit_exit();
	return 0;

error:
	crit_exit();
	return err;
}


static int
vmx_handle_vmexit(void)
{
	struct vmx_thread_info * vti;
	int exit_reason;
	int exception_type;
	int exception_number;
	struct trapframe *tf_old;
	int err;
	int func, regs[4];
	struct lwp *lp = curthread->td_lwp;
	dkprintf("VMM: handle_vmx_vmexit\n");

	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);


	exit_reason = VMCS_BASIC_EXIT_REASON(vti->vmexit_reason);

	switch (exit_reason) {
		case EXIT_REASON_EXCEPTION:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXCEPTION with qualification "
			    "%llx, interruption info %llx, interruption error %llx, instruction "
			    "length %llx\n",
			    (long long) vti->vmexit_qualification,
			    (long long) vti->vmexit_interruption_info,
			    (long long) vti->vmexit_interruption_error,
			    (long long) vti->vmexit_instruction_length);

			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXCEPTION rax: %llx, rip: %llx, "
			    "rsp: %llx,  rdi: %llx, rsi: %llx\n",
			    (long long)vti->guest.tf_rax,
			    (long long)vti->guest.tf_rip,
			    (long long)vti->guest.tf_rsp,
			    (long long)vti->guest.tf_rdi,
			    (long long)vti->guest.tf_rsi);

			exception_type = VMCS_EXCEPTION_TYPE(vti->vmexit_interruption_info);
			exception_number = VMCS_EXCEPTION_NUMBER(vti->vmexit_interruption_info);

			if (exception_type == VMCS_EXCEPTION_HARDWARE) {
				switch (exception_number) {
					case IDT_UD:
						dkprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE IDT_UD\n");
#ifdef VMM_DEBUG
						uint8_t instruction[INSTRUCTION_MAX_LENGTH];
						/* Check to see if its syscall asm instuction */
						user_vkernel_copyin(
						    (const void *) vti->guest.tf_rip,
						    instruction,
						    vti->vmexit_instruction_length);
						
						if (instr_check(&syscall_asm,
						    (void *) instruction,
						    (uint8_t) vti->vmexit_instruction_length)) {
							kprintf("VMM: handle_vmx_vmexit: UD different from syscall: ");
							db_disasm((db_addr_t) instruction, FALSE, NULL);
						}
#endif
						vti->guest.tf_err = 2;
						vti->guest.tf_trapno = T_FAST_SYSCALL;
						vti->guest.tf_xflags = 0;

						vti->guest.tf_rip += vti->vmexit_instruction_length;

						tf_old = lp->lwp_md.md_regs;
						lp->lwp_md.md_regs = &vti->guest;
						syscall2(&vti->guest);
						lp->lwp_md.md_regs = tf_old;

						break;
					case IDT_PF:
						dkprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE IDT_PF at %llx\n",
						    (long long) vti->guest.tf_rip);

						if (vti->guest.tf_rip == 0) {
							kprintf("VMM: handle_vmx_vmexit: Terminating...\n");
							err = -1;
							goto error;
						}

						vti->guest.tf_err = vti->vmexit_interruption_error;
						vti->guest.tf_addr = vti->vmexit_qualification;
						vti->guest.tf_xflags = 0;
						vti->guest.tf_trapno = T_PAGEFLT;

						tf_old = curthread->td_lwp->lwp_md.md_regs;
						curthread->td_lwp->lwp_md.md_regs = &vti->guest;
						trap(&vti->guest);
						curthread->td_lwp->lwp_md.md_regs = tf_old;

						break;
					default:
						kprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE unknown "
						    "number %d rip: %llx, rsp: %llx\n", exception_number,
						    (long long)vti->guest.tf_rip, (long long)vti->guest.tf_rsp);
						err = -1;
						goto error;
				}
			} else {
				kprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_ %d unknown\n", exception_type);
				err = -1;
				goto error;
			}
			break;
		case EXIT_REASON_EXT_INTR:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXT_INTR\n");
			break;
		case EXIT_REASON_CPUID:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_CPUID\n");

			func = vti->guest.tf_rax;

			do_cpuid(func, regs);

			vti->guest.tf_rax = regs[0];
			vti->guest.tf_rbx = regs[1];
			vti->guest.tf_rcx = regs[2];
			vti->guest.tf_rdx = regs[3];

			vti->guest.tf_rip += vti->vmexit_instruction_length;

			break;
		default:
			kprintf("VMM: handle_vmx_vmexit: unknown exit reason: %d with qualification %lld\n",
			    exit_reason, (long long) vti->vmexit_qualification);
			err = -1;
			goto error;
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
	int err;
	int ret;
	uint64_t val;
	struct trapframe *tmp;

	regular2ept_pmap();

restart:
	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR2_IF(vti == NULL);
	ERROR2_IF(vmx_check_cpu_migration());
	ERROR2_IF(vmx_handle_cpu_migration());

	crit_enter();
	cpu_disable_intr();
	splz();
	if (gd->gd_reqflags & RQF_AST_MASK) {
		atomic_clear_int(&gd->gd_reqflags, RQF_AST_SIGNAL);
		tmp = curthread->td_lwp->lwp_md.md_regs;
		curthread->td_lwp->lwp_md.md_regs = &vti->guest;
		cpu_enable_intr();
		crit_exit();
		vti->guest.tf_trapno = T_ASTFLT;
		trap(&vti->guest);
		/* CURRENT CPU CAN CHANGE */
		curthread->td_lwp->lwp_md.md_regs = tmp;
		goto restart;
	}
	if (vti->last_cpu != gd->gd_cpuid) {
		cpu_enable_intr();
		crit_exit();
		kprintf("VMM: vmx_vmrun: vti unexpectedly changed cpus %d->%d\n",
			gd->gd_cpuid, vti->last_cpu);
		goto restart;
	}

	/*
	 * Load specific Guest registers
	 * GP registers will be loaded in vmx_launch/resume
	 */
	ERROR_IF(vmwrite(VMCS_GUEST_RIP, vti->guest.tf_rip));
	ERROR_IF(vmwrite(VMCS_GUEST_CS_SELECTOR, vti->guest.tf_cs));
	ERROR_IF(vmwrite(VMCS_GUEST_RFLAGS, vti->guest.tf_rflags));
	ERROR_IF(vmwrite(VMCS_GUEST_RSP, vti->guest.tf_rsp));
	ERROR_IF(vmwrite(VMCS_GUEST_SS_SELECTOR, vti->guest.tf_ss));
	ERROR_IF(vmwrite(VMCS_GUEST_CR3, (uint64_t) curthread->td_pcb->pcb_cr3));

	/*
	 * The kernel caches the MSR_FSBASE value in mdcpu->gd_user_fs.
	 * A vmexit loads this unconditionally from the VMCS so make
	 * sure it loads the correct value.
	 */
	ERROR_IF(vmwrite(VMCS_HOST_FS_BASE, mdcpu->gd_user_fs));

	/*
	 * This can be changed by the vmspace_ctl syscall - update the
	 * VMCS field accordingly to the restored correctly by the VMX
	 * module
	 */
	ERROR_IF(vmwrite(VMCS_HOST_CR3, (uint64_t) rcr3()));

	if (vti->launched) { /* vmresume */
		dkprintf("\n\nVMM: vmx_vmrun: vmx_resume\n");
		ret = vmx_resume(vti);

	} else { /* vmlaunch */
		dkprintf("\n\nVMM: vmx_vmrun: vmx_launch\n");
		vti->launched = 1;
		ret = vmx_launch(vti);
	}
	if (ret == VM_EXIT) {

		ERROR_IF(vmx_vmexit_loadinfo());

		cpu_enable_intr();
		crit_exit();

		if (vmx_handle_vmexit())
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
	ept2regular_pmap();
	kprintf("VMM: vmx_vmrun: returning with success\n");
	return 0;

error:
	cpu_enable_intr();
	crit_exit();
error2:
	ept2regular_pmap();
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
	.vm_set_tls_area = vmx_set_tls_area,
};

struct vmm_ctl*
get_ctl_intel(void){
	return &ctl_vmx;
}
