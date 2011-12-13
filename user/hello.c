#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	envid_t id;
	int r;

	id = fork();
	
	if (!id) {
		while((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
		}
	}

	id = fork();

	if (!id) {	
		while ((r = sys_migrate(&thisenv)) < 0) {
			sys_yield();
		}
		
		cprintf("===> Hello World! I am child at %x.\n", 
			thisenv->env_id);

		cprintf("===> Got %x from %x.\n", r, id);
		r = ipc_recv(&id, NULL, NULL);

		ipc_send(thisenv->env_parent_id, 0x200, NULL, 0);
		cprintf("===> Sending 200 to parent at %x.\n", 
			thisenv->env_parent_id);
	}
	else {    
		cprintf("===> Hello World! I am parent at %x.\n", 
			thisenv->env_id);

		cprintf("===> Sending 100 to child at %x.\n", id);
		ipc_send(id, 0x100, NULL, 0);

		r = ipc_recv(&id, NULL, NULL);
		cprintf("===> Got %x from %x.\n", r, id);
	}
}
