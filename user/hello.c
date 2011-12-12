// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id;
	int val;

	id = fork();
	if (!id) {
		sys_migrate(&thisenv);
		cprintf("hello world! i am child environment %08x\n", 
			thisenv->env_id);
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("parent sent me %x\n", val);

		ipc_send(thisenv->env_parent_id, 02200, NULL, 0x0);
		cprintf("sending parent 200\n");
		
		val = ipc_recv(NULL, NULL, NULL);
                cprintf("parent sent me %x\n", val);

		id = fork();
		if (!id) {
			while (sys_migrate(&thisenv) != 0) {
				cprintf("retrying migrate...\n");
			}

			cprintf("hello world! i am grand child!");
		}
	}
	else {
		cprintf("hello world! i am parent environment %08x\n", 
			thisenv->env_id);
		ipc_send(id, 0x100, NULL, 0x0);
		cprintf("send child 100\n");
		
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("child sent me %x\n", val);

		ipc_send(id, 0x300, NULL, 0x0);
		cprintf("send child 300\n");
	}
}
