// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
/*	// 1
	cprintf("===> Now you don't see me...\n");
	sys_migrate(&thisenv);
	cprintf("===> Now you do!\n");
*/
/*
	// 2
	int id;
	cprintf("===> Watch closely...\n");
	id = fork();
	if (!id) {
		sys_migrate(&thisenv);
	}

	cprintf("===> Time for the prestige!\n");
*/
        // 3
	int id;
	int val;

	id = fork();
	if (!id) {
		sys_migrate(&thisenv);
		cprintf("===> hello world! i am child environment %08x\n", 
			thisenv->env_id);
		val = ipc_recv(NULL, NULL, NULL);
		cprintf("===> parent sent me %x\n", val);
	}
	else {
		cprintf("===> hello world! i am parent environment %08x\n", 
			thisenv->env_id);
		ipc_send(id, 0x100, NULL, 0x0);
		cprintf("===> send child 100\n");
	}

}
