#ifndef _VMM_VMX_INSTR_H_
#define _VMM_VMX_INSTR_H_

#include <vm/pmap.h>

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
	"ok:		mov $0, %[err];"	\
	"end:"

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
vmptrld(char *vmcs)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmcs);
	__asm __volatile("vmptrld %[paddr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [paddr] "m" (paddr)
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

	__asm __volatile("vmread %[reg], %[addr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [reg] "r" (reg), [addr] "m" (*addr)
			 : "memory");

	return err;
}
#endif
