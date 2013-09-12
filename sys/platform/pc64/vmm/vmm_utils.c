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

static int
get_pt_entry(struct vmspace *vm, pt_entry_t *pte, vm_offset_t addr, int index)
{
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	pt_entry_t *pt;
	int err = 0;
	vm_page_t m;

	m = vm_fault_page(&vm->vm_map, trunc_page(addr),
	    VM_PROT_READ, VM_FAULT_NORMAL, &err);
	if (err) {
		kprintf("%s: could not get addr %llx\n",
		    __func__, (unsigned long long)addr);
		goto error;
	}
	lwb = lwbuf_alloc(m, &lwb_cache);
	pt = (pt_entry_t *)lwbuf_kva(lwb) + ((vm_offset_t)addr & PAGE_MASK);

	*pte = pt[index];
	lwbuf_free(lwb);
	vm_page_unhold(m);
error:
	return err;
}

int
guest_phys_addr(struct vmspace *vm, register_t *gpa, register_t guest_cr3, vm_offset_t uaddr)
{
	pt_entry_t pml4e;
	pt_entry_t pdpe;
	pt_entry_t pde;
	pt_entry_t pte;
	int err = 0;

	err = get_pt_entry(vm, &pml4e, guest_cr3, uaddr >> PML4SHIFT);
	if (err) {
		kprintf("%s: could not get pml4e\n", __func__);
		goto error;
	}
	if (pml4e & kernel_pmap.pmap_bits[PG_V_IDX]) {
		err = get_pt_entry(vm, &pdpe, pml4e & PG_FRAME, (uaddr & PML4MASK) >> PDPSHIFT);
		if (err) {
			kprintf("%s: could not get pdpe\n", __func__);
			goto error;
		}
		if (pdpe & kernel_pmap.pmap_bits[PG_V_IDX]) {
			if (pdpe & kernel_pmap.pmap_bits[PG_PS_IDX]) {
				*gpa = (pdpe & PG_FRAME) | (uaddr & PDPMASK);
				goto out;
			} else {
				err = get_pt_entry(vm, &pde, pdpe & PG_FRAME, (uaddr & PDPMASK) >> PDRSHIFT);
				if(err) {
					kprintf("%s: could not get pdpe\n", __func__);
					goto error;
				}
				if (pde & kernel_pmap.pmap_bits[PG_V_IDX]) {
					if (pde & kernel_pmap.pmap_bits[PG_PS_IDX]) {
						*gpa = (pde & PG_FRAME) | (uaddr & PDRMASK);
						goto out;
					} else {
						err = get_pt_entry(vm, &pte, pde & PG_FRAME, (uaddr & PDRMASK) >> PAGE_SHIFT);
						if (err) {
							kprintf("%s: could not get pte\n", __func__);
							goto error;
						}
						if (pte & kernel_pmap.pmap_bits[PG_V_IDX]) {
							*gpa = (pte & PG_FRAME) | (uaddr & PAGE_MASK);
						} else {
							kprintf("%s: pte not valid\n", __func__);
							err = EFAULT;
							goto error;
						}
					}
				} else {
					kprintf("%s: pde not valid\n", __func__);
					err = EFAULT;
					goto error;
				}
			}
		} else {
			kprintf("%s: pdpe not valid\n", __func__);
			err = EFAULT;
			goto error;
		}
	} else {
		kprintf("%s: pml4e not valid\n", __func__);
		err = EFAULT;
		goto error;
	}
out:
error:
	return err;
}
