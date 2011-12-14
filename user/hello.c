#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id, val;
	
	id = fork();

	if (!id) {
		while (sys_migrate(&thisenv) < 0);
		cprintf("===> Hello World! I am child environment %x.\n", 
			thisenv->env_id);

		val = ipc_recv(NULL, NULL, NULL);
		cprintf("===> Got %x from %x.\n", val, thisenv->env_ipc_from);
	}
	else {
		cprintf("===> Hello World! I am parent environment %x.\n", 
		thisenv->env_id);
		val = 0x100;
		ipc_send(id, 0x100, NULL, 0);
		cprintf("===> Sent %x to %x.\n", 0x100, id);		
	}
}
