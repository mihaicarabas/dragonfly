#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmm_guest_ctl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <stdarg.h>
#define vmm_printf(val, err, exp) \
	printf("vmm_guest(%d): return %d, expected %d\n", val, err, exp);

void test() {
	int a;
	int b;
	int c;
	int d;
	int e;

	printf("%x %x %x %x %x\n", &a, &b, &c, &d, &e);
	FILE *f = fopen("test.test","w");
	fprintf(f,"test");
	fclose(f);
}
int
main(void)
{
	int error = 0;
	int mib[3];
	size_t len = 3;
	int enable = 0;
	int enabl2 = 0;
	void *stack;
	struct guest_options options;

//	sysctlnametomib("hw.vmm.enable", mib, &len);
//	test();
//	error = vmm_guest_ctl(100);
//	vmm_printf(100, error, -1);

	/* Disable vmm and make the vmm_guest syscall */
//	printf ("hw.vmm.enable = 0\n");
//	len = sizeof(int);
//	if (sysctl(mib, 3, &enabl2, &len, &enable, len) == -1)
//		perror("sysctl");

//	error = vmm_guest_ctl(VMM_GUEST_INIT);
//	vmm_printf(VMM_GUEST_INIT, error, -1);

	/* Enable vmm and make the vmm_guest syscall */
//	printf ("hw.vmm.enable = 1\n");
//	enable = 1;
//	if (sysctl(mib, 3, NULL, NULL, &enable, sizeof(int)) == -1)
//		perror("sysctl");

	stack = mmap(NULL, 64 * PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON, -1, 0);

	if (stack == MAP_FAILED) {
		printf("Error on allocating stack\n");
		return -1;
	}

	options.ip = (register_t) test;
	options.sp = (register_t) ((uint64_t)stack + 64 * PAGE_SIZE - sizeof(register_t));

	error = vmm_guest_ctl(VMM_GUEST_INIT, &options);
	vmm_printf(VMM_GUEST_INIT, error, 0);

	error = vmm_guest_ctl(VMM_GUEST_RUN, NULL);
	vmm_printf(VMM_GUEST_RUN, error, 0);

	error = vmm_guest_ctl(VMM_GUEST_DESTROY, NULL);
	vmm_printf(VMM_GUEST_DESTROY, error, 0);

	return 0;
}
