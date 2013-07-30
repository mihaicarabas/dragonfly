#include <sys/types.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <cpu/lwbuf.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include "vmm_utils.h"

int
instr_check(struct instr_decode *instr, void *ip, uint8_t instr_length)
{

	uint8_t i;
	uint8_t *instr_ip;
	uint8_t *instr_opcode;

	instr_ip = (uint8_t *) ip;
	instr_opcode = (uint8_t *) &instr->opcode;

	/*  Skip REX prefix if present */
	if (*instr_ip >= 0x40 && *instr_ip <= 0x4F) {
		instr_ip++;
		instr_length--;
	}

	for (i = 0; i < instr->opcode_bytes; i++) {
		if (i < instr_length) {
			if (instr_ip[i] != instr_opcode[i]) {
				return -1;
			}
		} else {
			return -1;
		}
	}
	return 0;
}

int
user_vkernel_copyin(const void *udaddr, void *kaddr, size_t len)
{
	struct vmspace *vm = curthread->td_lwp->lwp_vmspace;
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	vm_page_t m;
	int error;
	size_t n;

	error = 0;
	while (len) {
		m = vm_fault_page(&vm->vm_map, trunc_page((vm_offset_t)udaddr),
				  VM_PROT_READ,
				  VM_FAULT_NORMAL, &error);
		if (error)
			break;
		n = PAGE_SIZE - ((vm_offset_t)udaddr & PAGE_MASK);
		if (n > len)
			n = len;
		lwb = lwbuf_alloc(m, &lwb_cache);
		bcopy((char *)lwbuf_kva(lwb)+((vm_offset_t)udaddr & PAGE_MASK),
		      kaddr, n);
		len -= n;
		udaddr = (const char *)udaddr + n;
		kaddr = (char *)kaddr + n;
		lwbuf_free(lwb);
		vm_page_unhold(m);
	}
	if (error)
		error = EFAULT;
	return (error);
}

