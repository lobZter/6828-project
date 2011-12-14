#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id, val;
	
	id = fork();

	while (sys_migrate(&thisenv) < 0) {
		sys_yield();
	}

	if (!id) {
		cprintf("===> Hello World! I am child.\n");
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("===> I got %x from %x.\n", val, 
			thisenv->env_ipc_from);
	}
	else {
		cprintf("===> Hello World! I am parent.\n");
		val = 0x100;
		ipc_send(id, val, NULL, 0);
		cprintf("===> I sent %x.\n", val);
	}
}
