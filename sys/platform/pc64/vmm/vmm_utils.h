#ifndef _VMM_VMM_UTILS_H_
#define _VMM_VMM_UTILS_H_

struct instr_decode {
	uint8_t opcode_bytes;
	struct {
		uint8_t byte1;
		uint8_t byte2;
		uint8_t byte3;
	} opcode;
};

int
user_vkernel_copyin(const void *udaddr, void *kaddr, size_t len);
int instr_check(struct instr_decode *instr, void *ip, uint8_t instr_length);
int guest_phys_addr(struct vmspace *vm, register_t *gpa, register_t guest_cr3, vm_offset_t uaddr);
#endif
