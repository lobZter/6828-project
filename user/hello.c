// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id;
	int val;

	id = fork();
	if (id == 0) {
		sys_migrate(&thisenv);
		cprintf("hello world! i am child environment %08x\n", 
			thisenv->env_id);
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("papa sent me %x\n", val);

		ipc_send(thisenv->env_parent_id, 0x200, NULL, 0x0);
		cprintf("send parent 0x200\n");
		
	}
	else {
		cprintf("hello world! i am parent environment %08x\n", 
			thisenv->env_id);
		ipc_send(id, 0x100, NULL, 0x0);
		cprintf("send child 0x100\n");
		
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("child sent me %x\n", val);
	}
}
