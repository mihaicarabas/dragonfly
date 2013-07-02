#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmm_guest_ctl.h>
#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>

#define vmm_printf(val, err, exp) \
	printf("vmm_guest(%d): return %d, expected %d\n", val, err, exp);
int
main(void)
{
	int error = 0;
	int mib[3];
	size_t len = 3;
	int enable = 0;
	int enabl2 = 0;
	sysctlnametomib("hw.vmm.enable", mib, &len);

	/* Disable vmm and make the vmm_guest syscall */
	printf ("hw.vmm.enable = 0\n");
	len = sizeof(int);
	if (sysctl(mib, 3, &enabl2, &len, &enable, len) == -1)
		perror("sysctl");

	error = vmm_guest_ctl(100);
	vmm_printf(100, error, -1);

	error = vmm_guest_ctl(VMM_GUEST_INIT);
	vmm_printf(VMM_GUEST_INIT, error, -1);

	/* Enable vmm and make the vmm_guest syscall */
	printf ("hw.vmm.enable = 1\n");
	enable = 1;
	if (sysctl(mib, 3, NULL, NULL, &enable, sizeof(int)) == -1)
		perror("sysctl");

	error = vmm_guest_ctl(100);
	vmm_printf(100, error, -1);

	error = vmm_guest_ctl(VMM_GUEST_INIT);
	vmm_printf(VMM_GUEST_INIT, error, 0);

	return 0;
}
