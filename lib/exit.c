
#include <inc/lib.h>

void
exit(void)
{
	// Check if is leased task and completed
	if (thisenv->env_alien) {
		sys_lease_complete();
	}

	close_all();
	sys_env_destroy(0);
}

