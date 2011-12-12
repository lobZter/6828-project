// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int id;
	id = fork();

	if (id == 0) {
		sys_migrate(&thisenv);
		cprintf("hello world! i am child environment %08x\n", 
			thisenv->env_id);
	}
	else {
		cprintf("hello world! i am parent environment %08x\n", 
			thisenv->env_id);
	}
}
