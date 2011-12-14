#include <inc/lib.h>

char s[] = "Hello World!";

void
umain(int argc, char **argv)
{
	int r, id;
	int val;

	id = fork();

	if (!id) {
		while (sys_migrate(&thisenv) < 0) {
			sys_yield();
		}

		cprintf("===> (%x) I am child.\n", thisenv->env_id);
		r = sys_page_alloc(0, (void *) (USTACKTOP - 2*PGSIZE), 
				   PTE_U | PTE_P | PTE_W);

		if (r < 0) {
			cprintf("Failed to alloc mem! %d\n", r);
			exit();
		}

		memmove((void *) (USTACKTOP - 2*PGSIZE), (void *) s, 
			12);

		ipc_send(thisenv->env_parent_id, 12, 
			 (void *) (USTACKTOP - 2*PGSIZE),
			 PTE_U|PTE_P|PTE_W);

		cprintf("===> (%x) I sent Hello World! to (%x).\n", 
			thisenv->env_id, thisenv->env_parent_id);
	}
	else {
		cprintf("===> (%x) I am parent.\n", thisenv->env_id);
		val = ipc_recv(NULL, (void *) (USTACKTOP - 2*PGSIZE), NULL);
		cprintf("===> (%x) Got byte string of length %d.\n", 
			thisenv->env_id, val);
		cprintf("===> (%x) String is \n", thisenv->env_id);
		sys_cputs((char *) (USTACKTOP - 2*PGSIZE), val);
		cprintf("\n");
	}
}
