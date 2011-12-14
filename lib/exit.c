
#include <inc/lib.h>

void
exit(void)
{
	// Check if is leased task and completed
	if (thisenv->env_alien) {
		cprintf("fucking a %x\n", thisenv->env_id);
		while (sys_lease_complete() < 0) {
			cprintf("boom\n");
		}
		cprintf("lalalala\n");
	}
	
	close_all();
	sys_env_destroy(0);
}

