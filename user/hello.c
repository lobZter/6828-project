// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	while(sys_migrate()) {

	}

//	sys_migrate();
	cprintf("hello world! i am environment %08x\n", thisenv->env_id);
}
