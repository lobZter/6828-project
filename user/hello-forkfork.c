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

		id = fork();
		
		if (!id) {	
			while ((r = sys_migrate(&thisenv)) < 0) {
				sys_yield();
			}
		
			cprintf("===> Hello World! I am grand child at %x.\n", 
				thisenv->env_id);

			r = 0x100;
			ipc_send(thisenv->env_parent_id, r, NULL, 0);
			cprintf("===> Sent %x to %x from %x.\n", r, 
				thisenv->env_parent_id, thisenv->env_id);
		}
		else {
			cprintf("===> Hello World! I am child at %x.\n", 
				thisenv->env_id);
			
			r = ipc_recv(&id, NULL, NULL);
			cprintf("===> Got %x from %x to %x.\n", r, id,
				thisenv->env_id);

			r = r + 0x100;
			ipc_send(thisenv->env_parent_id, r, NULL, 0);
			cprintf("===> Sent %x to %x from %x.\n", r,
				thisenv->env_parent_id, thisenv->env_id);
		}
	}
	else {    
		cprintf("===> Hello World! I am parent at %x.\n", 
			thisenv->env_id);

		r = ipc_recv(&id, NULL, NULL);
		cprintf("===> Got %x from %x to %x.\n", r, id,
			thisenv->env_id);
	}

}
