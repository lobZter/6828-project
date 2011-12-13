#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id, r;

	id = fork();
	
	if (!id) {
		while ((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
			cprintf("===> Child retrying migrate...\n");
		}

		cprintf("===> World %d\n", r);
	}
	else {    
		cprintf("===> Hello\n");

		while ((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
			cprintf("===> Parent retrying migrate...\n");
		}

		cprintf("===> DeadBeef %d\n", r);
	}
}
