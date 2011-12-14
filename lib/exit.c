
#include <inc/lib.h>

void
exit(void)
{
	// Check if is leased task and completed
	if (thisenv->env_alien) {
		while (sys_lease_complete() < 0);
	}
	cprintf("ooo\n");
	close_all();
	sys_env_destroy(0);
}

