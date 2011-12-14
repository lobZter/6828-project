#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int n = 2;
	int id, val, m;

	while (n > 0) {
		if (n == 1) {
			cprintf("==> (%x) I am env %d.\n", 
				thisenv->env_id, 1);
			ipc_send(thisenv->env_parent_id, 1, NULL, 0);
			exit();
		}

		id = fork();

		if (!id) {
			while (sys_migrate(&thisenv) < 0) {
				sys_yield();
			}
			n--;
			continue;
		}
		else {
			m = n;
			break;
		}
	}

	cprintf("===> (%x) I am env %d.\n", thisenv->env_id, m);
	val = ipc_recv(NULL, NULL, NULL);
	cprintf("===> (%x) %d! is %d.\n", thisenv->env_id, m, m*val);

	if (thisenv->env_parent_id != 0) {
		ipc_send(thisenv->env_parent_id, val, NULL, 0);
		cprintf("===> (%x) I sent %x! to %x.\n", m, 
			thisenv->env_parent_id);
	}
}
