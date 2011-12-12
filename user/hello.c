#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf("Hello\n");
	sys_migrate(&thisenv);
	cprintf("World\n");
}
