#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int n = 3;
	int k = 2;
	int id, val1, val2, m = 0;

	while (n > 0) {
		if (n == 1) {
			cprintf("==> (%x) I am env %d.\n", 
				thisenv->env_id, 1);
			ipc_send(thisenv->env_parent_id, 2, NULL, 0);
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
			id = fork();
			
			if (!id) {
				while (sys_migrate(&thisenv) < 0) {
					sys_yield();
				}
				n--;
				continue;
			}

			m = n;
			break;
		}
	}

	cprintf("===> (%x) I am env %d.\n", thisenv->env_id, m);
	val1 = ipc_recv(NULL, NULL, NULL);
	val2 = ipc_recv(NULL, NULL, NULL);
	cprintf("===> (%x) %d^%d is %d.\n", thisenv->env_id, k, m, val1*val2);

	if (thisenv->env_parent_id != 0) {
		ipc_send(thisenv->env_parent_id, val2*val1, NULL, 0);
		cprintf("===> (%x) I sent %d^%d! to %x.\n", 
			thisenv->env_id, k, m, 
			thisenv->env_parent_id);
	}
}
