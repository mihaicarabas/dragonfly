#ifndef _VMM_EPT_H_
#define _VMM_EPT_H_

#include <vm/vm.h>

#include <machine/pmap.h>

/* EPT defines */
#define	EPT_PWL4(cap)			((cap) & (1ULL << 6))
#define	EPT_MEMORY_TYPE_WB(cap)		((cap) & (1UL << 14))
#define	EPT_AD_BITS_SUPPORTED(cap)	((cap) & (1ULL << 21))
#define	EPT_PG_READ			(0x1ULL << 0)
#define	EPT_PG_WRITE			(0x1ULL << 1)
#define	EPT_PG_EXECUTE			(0x1ULL << 2)
#define	EPT_IGNORE_PAT			(0x1ULL << 6)
#define	EPT_PG_PS			(0x1ULL << 7)
#define	EPT_PG_A			(0x1ULL << 8)
#define	EPT_PG_M			(0x1ULL << 9)
#define	EPT_PG_AVAIL1			(0x1ULL << 10)
#define	EPT_PG_AVAIL2			(0x1ULL << 11)
#define	EPT_PG_AVAIL3			(0x1ULL << 52)
#define	EPT_PWLEVELS			(4)	/* page walk levels */

#define	EPTP_CACHE(x)			(x)
#define	EPTP_PWLEN(x)			((x) << 3)
#define	EPTP_AD_ENABLE			(0x1ULL << 6)

#define	EPT_MEM_TYPE_SHIFT		(0x3)
#define	EPT_MEM_TYPE_UC			(0x0ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WC			(0x1ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WT			(0x4ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WP			(0x5ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WB			(0x6ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_MASK		(0x7ULL << EPT_MEM_TYPE_SHIFT)

#define	EPT_VIOLATION_READ		(1ULL << 0)
#define	EPT_VIOLATION_WRITE		(1ULL << 1)
#define	EPT_VIOLATION_INST_FETCH	(1ULL << 2)
#define	EPT_VIOLATION_GPA_READABLE	(1ULL << 3)
#define	EPT_VIOLATION_GPA_WRITEABLE	(1ULL << 4)
#define	EPT_VIOLATION_GPA_EXECUTABLE	(1ULL << 5)

int vmx_ept_init(void);
void vmx_ept_pmap_pinit(pmap_t pmap);
uint64_t vmx_eptp(uint64_t ept_address);

static __inline int
vmx_ept_fault_type(uint64_t qualification){
	if (qualification & EPT_VIOLATION_WRITE)
		return VM_PROT_WRITE;
	else if (qualification & EPT_VIOLATION_INST_FETCH)
		return VM_PROT_EXECUTE;
	else
		return VM_PROT_READ;
}

static __inline int
vmx_ept_gpa_prot(uint64_t qualification){
	int prot = 0;

	if (qualification & EPT_VIOLATION_GPA_READABLE)
		prot |= VM_PROT_READ;

	if (qualification & EPT_VIOLATION_GPA_WRITEABLE)
		prot |= VM_PROT_WRITE;

	if (qualification & EPT_VIOLATION_GPA_EXECUTABLE)
		prot |= VM_PROT_EXECUTE;

	return prot;
}

#endif
