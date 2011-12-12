
#include <inc/lib.h>

void
exit(void)
{
	close_all();

	// Check if is leased task and completed
	if (thisenv->env_alien) {
		sys_lease_complete();
	}

	sys_env_destroy(0);
}

