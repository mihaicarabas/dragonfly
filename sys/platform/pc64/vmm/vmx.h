#ifndef _VMM_VMX_H_
#define _VMM_VMX_H_


/*
 * Chapter 30 VMX Instruction Reference
 * Section 30.3 "Conventions"
 * from Intel Architecture Manual 3C.
 */
#define	VM_SUCCEED		0
#define	VM_FAIL_INVALID		1
#define	VM_FAIL_VALID		2
#define	GET_ERROR_CODE				\
	"check_CF:	jnc check_ZF;"		\
	"		mov $1, %[err];"	\
	"		jmp ok;"		\
	"check_ZF:	jnz ok;"		\
	"		mov $2, %[err];"	\
	"		jmp end;"		\
	"ok:		mov $0, %[err];		\
	"end:"
#endif

static inline int
vmxon(char *vmx_region)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmx_region);
	__asm __volatile("vmxon %[paddr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [paddr] "m" (paddr)
			 : "memory");

	return err;
}

static inline void
vmxoff(void)
{

	__asm __volatile("vmxoff");
}

static inline void
vmptrst(uint64_t *addr)
{

	__asm __volatile("vmptrst %[addr]"
			:
			: [addr] "m" (*addr)
			: "memory");
}

static inline int
vmptrld(struct char *vmcs)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmcs);
	__asm __volatile("vmptrld %[addr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [addr] "m" (paddr)
			 : "memory");
	return err;
}

static inline int
vmwrite(uint64_t reg, uint64_t val)
{
	int err;

	__asm __volatile("vmwrite %[val], %[reg];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [val] "r" (val), [reg] "r" (reg)
			 : "memory");

	return err;
}

static inline int
vmread(uint64_t reg, uint64_t *addr)
{
	int err;

	__asm __volatile("vmread %[r], %[addr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [reg] "r" (reg), [addr] "m" (*addr)
			 : "memory");

	return err;
}



/* MSR registers */

#define		IA32_VMX_BASIC			0x480
#define		IA32_VMX_PINBASED_CTLS		0x481
#define		IA32_VMX_PROCBASED_CTLS		0x482
#define		IA32_VMX_EXIT_CTLS		0x483
#define		IA32_VMX_ENTRY_CTLS		0x484
#define		IA32_VMX_CR0_FIXED0		0x486
#define		IA32_VMX_CR0_FIXED1		0x487
#define		IA32_VMX_CR4_FIXED0		0x488
#define		IA32_VMX_CR4_FIXED1		0x489
#define		IA32_VMX_TRUE_ENTRY_CTLS	0x490
#define		IA32_VMX_PROCBASED_CTLS2	0x48B
#define		IA32_VMX_EPT_VPID_CAP		0x48C
#define		IA32_VMX_TRUE_PINBASED_CTLS	0x48D
#define		IA32_VMX_TRUE_PROCBASED_CTLS	0x48E
#define		IA32_VMX_TRUE_EXIT_CTLS		0x48F

#define IS_ONE_SETTING_ALLOWED(val, bit)	\
    ((val) & (1ULL << (bit + 32)))

#define IS_ZERO_SETTING_ALLOWED(val, bit)	\
    ((val) & (1ULL << (bit)))

/*
 * A.1 BASIC VMX INFORMATION
 * from Intel Architecture Manual 3C.
 */
#define VMX_REVISION(reg_val)		(reg_val &0x7fffffff)		/* 0:30 */
#define VMX_REGION(reg_val)		((reg_val >> 32) &0x01ffffff)	/* 32:44 */
#define VMX_WIDTH_ADDR(reg_val)		(reg_val >> 48 & 0x1)		/* 48 */

