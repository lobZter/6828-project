#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id, val;
	
	id = fork();

	if (id) {
		while (sys_migrate(&thisenv) < 0);
		id = fork();
		
		if (!id) {
			while (sys_migrate(&thisenv) < 0);
		}
	}

	cprintf("===> Hello World! I am environment %x, with hosteid %x.\n",
		thisenv->env_id, thisenv->env_hosteid);
}
