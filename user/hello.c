#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id, r;

	id = fork();
	
	if (!id) {
		while ((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
		}

		cprintf("===> World %d\n", r);

		id = fork();
		
		if (!id) {
			while ((r = sys_migrate(&thisenv)) < 0) {
				sys_yield();
			}

			cprintf("===> I'm back! %d\n", r);
		}
	}
	else {    
		cprintf("===> Hello\n");

		while ((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
		}

		cprintf("===> DeadBeef %d\n", r);
	}
}
