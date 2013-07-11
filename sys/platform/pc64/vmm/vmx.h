#ifndef _VMM_VMX_H_
#define _VMM_VMX_H_



#define BIT(x)	(1ULL << x)


/* VMX info for a generic control */
struct vmx_ctl_info {
	uint32_t msr_addr;
	uint32_t msr_true_addr;
	uint32_t ctls;
};

/* The value of a setting */
typedef enum {
	ZERO,
	ONE,
	DEFAULT
} setting_t;

/* VMX per cpu info */
struct vmx_pcpu_info {
	unsigned char *vmxon_region_na;
	unsigned char *vmxon_region;
	unsigned char *loaded_vmcs;
};

struct vmx_thread_info {
	unsigned char *vmcs_region_na;
	unsigned char *vmcs_region;
	int launched;
	int last_cpu;
	/* Guest unsaved registers in VMCS */
	register_t	guest_rax;
	register_t	guest_rbx;
	register_t	guest_rcx;
	register_t	guest_rdx;
	register_t	guest_rsi;
	register_t	guest_rdi;
	register_t	guest_rbp;
	register_t	guest_r8;
	register_t	guest_r9;
	register_t	guest_r10;
	register_t	guest_r11;
	register_t	guest_r12;
	register_t	guest_r13;
	register_t	guest_r14;
	register_t	guest_r15;
	register_t	guest_cr2;

	/* Host unsaved registers in VMCS */
	register_t	host_rbx;
	register_t	host_rbp;
	register_t	host_r10;
	register_t	host_r11;
	register_t	host_r12;
	register_t	host_r13;
	register_t	host_r14;
	register_t	host_r15;
	register_t	host_rsp;

};

int vmx_launch(struct vmx_thread_info *);
int vmx_resume(struct vmx_thread_info *);
void vmx_vmexit(void);



/*
 * MSR register address
 */
#define		IA32_FEATURE_CONTROL			0x3A
#define		IA32_VMX_BASIC				0x480
#define		IA32_VMX_PINBASED_CTLS			0x481
#define		IA32_VMX_PROCBASED_CTLS			0x482
#define		IA32_VMX_EXIT_CTLS			0x483
#define		IA32_VMX_ENTRY_CTLS			0x484
#define		IA32_VMX_CR0_FIXED0			0x486
#define		IA32_VMX_CR0_FIXED1			0x487
#define		IA32_VMX_CR4_FIXED0			0x488
#define		IA32_VMX_CR4_FIXED1			0x489
#define		IA32_VMX_EPT_VPID_CAP			0x48C
#define		IA32_VMX_PROCBASED_CTLS2		0x48B
#define		IA32_VMX_TRUE_PINBASED_CTLS		0x48D
#define		IA32_VMX_TRUE_PROCBASED_CTLS		0x48E
#define		IA32_VMX_TRUE_EXIT_CTLS			0x48F
#define		IA32_VMX_TRUE_ENTRY_CTLS		0x490



/*
 * IA32 FEATURE CONTROL bits
 */
#define		FEATURE_CONTROL_LOCKED			0
#define		FEATURE_CONTROL_VMX_BIOS_ENABLED	2



/*
 * IA32_VMX_BASIC
 * A.1 BASIC VMX INFORMATION
 */
#define		IS_TRUE_CTL_AVAIL(VMX_BASIC)	((VMX_BASIC) & (1ULL << (55)))
#define		VMX_REVISION(reg_val)		(reg_val & 0x7fffffff) /* 0:30 */
#define 	VMX_REGION_SIZE(reg_val)	((reg_val >> 32) & 0x01fff) /* 32:44 */
#define 	VMX_WIDTH_ADDR(reg_val)		(reg_val >> 48 & 0x1) /* 48 */
#define		VMXON_REGION_ALIGN_SIZE		4096
#define		VMXON_REGION_ALIGN(p)		(((unsigned long long)(p) + VMXON_REGION_ALIGN_SIZE) & ~(VMXON_REGION_ALIGN_SIZE - 1))



/*
 * Pin-Based VM-Execution Controls
 * Table 24-5. Definitions of Pin-Based Controls
 * */
#define		PINBASED_EXTERNAL_INTERRUPT_EXITING	0
#define		PINBASED_NMI_EXITING			3
#define		PINBASED_VIRTUAL_NMIS			5
#define		PINBASED_ACTIVATE_VMX_PREEMPTION_TIEMR	6
#define		PINBASED_PROCESS_POSTED_INTERRUPTS	7



/*
 * Processor-Based VM-Execution Controls
 * Table 24-6. Definitions of Primary Processor-Based Controls
 */
#define		PROCBASED_INTERRUPT_WINDOW_EXITING	2
#define		PROCBASED_USE_TSC_OFFSETING		3
#define		PROCBASED_HLT_OFFSETING			7
#define		PROCBASED_INVLPG_EXITING		9
#define		PROCBASED_MWAIT_EXITING			10
#define		PROCBASED_RDPMC_EXITING			11
#define		PROCBASED_RDTSC_EXITING			12
#define		PROCBASED_CR3_LOAD_EXITING		15
#define		PROCBASED_CR3_STORE_EXITING		16
#define		PROCBASED_CR8_LOAD_EXITING		19
#define		PROCBASED_CR8_STORE_EXITING		20
#define		PROCBASED_USE_TPR_SHADOW		21
#define		PROCBASED_NMI_WINDOWS_EXITING		22
#define		PROCBASED_MOV_DR_EXITING		23
#define		PROCBASED_UNCOND_IO_EXITING		24
#define		PROCBASED_USE_IO_BITMAPS		25
#define		PROCBASED_MONITOR_TRAP_FLAG		27
#define		PROCBASED_USE_MSR_BITMAPS		28
#define		PROCBASED_MONITOR_EXITING		29
#define		PROCBASED_PAUSE_EXITING			30
#define		PROCBASED_ACTIVATE_SECONDARY_CONTROLS	31
/* Table 24-7. Definitions of Secondary Processor-Based Controls */
#define		PROCBASED2_VIRTUALIZE_APIC_ACCESSES	0
#define		PROCBASED2_ENABLE_EPT			1
#define		PROCBASED2_DESCRIPTOR_TABLE_EXITING	2
#define		PROCBASED2_ENABLE_RDTSCP		3
#define		PROCBASED2_VIRTUAL_x2APIC_MODE		4
#define		PROCBASED2_ENABLE_VPID			5
#define		PROCBASED2_WBINVD_EXITING		6
#define		PROCBASED2_UNRESTRICTED_GUEST		7
#define		PROCBASED2_APIC_REGISTER_VIRTULIZATION	8
#define		PROCBASED2_VIRTUAL_INTERRUPT_DELIVERY	9
#define		PROCBASED2_PAUSE_LOOP_EXITING		10
#define		PROCBASED2_RDRAND_EXITING		11
#define		PROCBASED2_ENABLE_INVPCID		12
#define		PROCBASED2_ENABLE_VM_FUNCTIONS		13
#define		PROCBASED2_VMCS_SHADOWING		14
#define		PROCBASED2_EPT_VIOLATION_VE		18



/*
 * VM-EXIT CONTROL FIELDS
 * Table 24-10. Definitions of VM-Exit Controls
 */
#define		VMEXIT_SAVE_DEBUG_CONTROLS		2
#define		VMEXIT_HOST_ADDRESS_SPACE_SIZE		9
#define		VMEXIT_LOAD_IA32_PERF_GLOBAL_CTRL	12
#define		VMEXIT_ACKNOWLEDGE_INTERRUPT_ON_EXIT	15
#define		VMEXIT_SAVE_IA32_PAT			18
#define		VMEXIT_LOAD_IA32_PAT			19
#define		VMEXIT_SAVE_IA32_EFER_PAT		20
#define		VMEXIT_LOAD_IA32_EFER_PAT		21
#define		VMEXIT_SAVE_VMX_PREEMPTION_TIMER	22



/*
 * VM-ENTRY CONTROL FIELDS
 * Table 24-12. Definitions of VM-Entry Controls
 */
#define		VMENTRY_LOAD_DEBUG_CONTROLS		2
#define		VMENTRY_IA32e_mode_guest		9
#define		VMENTRY_ENTRY_TO_SMM			10
#define		VMENTRY_DEACTIVATE_DUAL_MONITOR		11
#define		VMENTRY_LOAD_IA32_PERF_GLOBAL_CTRL	13
#define		VMENTRY_LOAD_IA32_PAT			14
#define		VMENTRY_LOAD_IA32_EFER			15



#define IS_ONE_SETTING_ALLOWED(val, bit)	\
    ((val) & (1ULL << (bit + 32)))

#define IS_ZERO_SETTING_ALLOWED(val, bit)	\
    (((val) & (1ULL << (bit))) == 0)



/*
 * VMX Basic Exit Reasons
 */
#define		EXIT_REASON_EXCEPTION		0
#define		EXIT_REASON_EXT_INTR		1
#define		EXIT_REASON_TRIPLE_FAULT	2
#define		EXIT_REASON_INIT		3
#define		EXIT_REASON_SIPI		4
#define		EXIT_REASON_IO_SMI		5
#define		EXIT_REASON_SMI			6
#define		EXIT_REASON_INTR_WINDOW		7
#define		EXIT_REASON_NMI_WINDOW		8
#define		EXIT_REASON_TASK_SWITCH		9
#define		EXIT_REASON_CPUID		10
#define		EXIT_REASON_GETSEC		11
#define		EXIT_REASON_HLT			12
#define		EXIT_REASON_INVD		13
#define		EXIT_REASON_INVLPG		14
#define		EXIT_REASON_RDPMC		15
#define		EXIT_REASON_RDTSC		16
#define		EXIT_REASON_RSM			17
#define		EXIT_REASON_VMCALL		18
#define		EXIT_REASON_VMCLEAR		19
#define		EXIT_REASON_VMLAUNCH		20
#define		EXIT_REASON_VMPTRLD		21
#define		EXIT_REASON_VMPTRST		22
#define		EXIT_REASON_VMREAD		23
#define		EXIT_REASON_VMRESUME		24
#define		EXIT_REASON_VMWRITE		25
#define		EXIT_REASON_VMXOFF		26
#define		EXIT_REASON_VMXON		27
#define		EXIT_REASON_CR_ACCESS		28
#define		EXIT_REASON_DR_ACCESS		29
#define		EXIT_REASON_INOUT		30
#define		EXIT_REASON_RDMSR		31
#define		EXIT_REASON_WRMSR		32
#define		EXIT_REASON_INVAL_VMCS		33
#define		EXIT_REASON_INVAL_MSR		34
#define		EXIT_REASON_MWAIT		36
#define		EXIT_REASON_MTF			37
#define		EXIT_REASON_MONITOR		39
#define		EXIT_REASON_PAUSE		40
#define		EXIT_REASON_MCE			41
#define		EXIT_REASON_TPR			43
#define		EXIT_REASON_APIC		44
#define		EXIT_REASON_GDTR_IDTR		46
#define		EXIT_REASON_LDTR_TR		47
#define		EXIT_REASON_EPT_FAULT		48
#define		EXIT_REASON_EPT_MISCONFIG	49
#define		EXIT_REASON_INVEPT		50
#define		EXIT_REASON_RDTSCP		51
#define		EXIT_REASON_VMX_PREEMPT		52
#define		EXIT_REASON_INVVPID		53
#define		EXIT_REASON_WBINVD		54
#define		EXIT_REASON_XSETBV		55
#define		EXIT_REASON_APIC_WRITE		56
#define		EXIT_REASON_RDRAND		57
#define		EXIT_REASON_INVPCID		58
#define		EXIT_REASON_VMFUNC		59


#endif
