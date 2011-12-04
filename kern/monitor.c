// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/types.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
        { "backtrace", "Display the current stack trace", mon_backtrace },
	{ "showmappings", "Display the current mapping from VA to PA", mon_showmappings },
	{ "setpgperms", "Sets the permissions for a page", mon_setpgperms },
	{ "examine", "Examine contents of VA range", mon_examine },
	{ "examinep", "Examine contents of PA range", mon_examinep },
	{ "step", "Step to next instruction in current env", mon_step },
	{ "continue", "Continue executing current env", mon_continue },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");

	int i;
	struct Eipdebuginfo eip_info;
	uint32_t *ebp = (uint32_t*) read_ebp();
	while (ebp != 0x0) {
		// Print ebp
		cprintf("  ebp %08x", ebp);

		// Print eip
		cprintf("  eip %08x", *(ebp + 1));

		// Print args
		cprintf("  args");
		for (i = 1; i < 6; i++) {
			cprintf(" %08x", *(ebp + 1 + i));
		}

		// Get eip debug info, and print it
		debuginfo_eip(*(ebp + 1), &eip_info);
		cprintf("         %s:%d: ", eip_info.eip_file, eip_info.eip_line);
		cprintf("%.*s", eip_info.eip_fn_namelen, eip_info.eip_fn_name);
		cprintf("+%d", *(ebp + 1) - eip_info.eip_fn_addr);

		// Print new line char
		cprintf("\n");

		// Move to old ebp
		ebp = (uint32_t*) *ebp;
	}

	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 2 && argc != 3) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}

	cprintf("VA        PA        U  W  P\n");
	uintptr_t va = ROUNDDOWN(strtol(argv[1], NULL, 16), PGSIZE); // start va
	uintptr_t end;
	
	// Just print one page mapping for one arg	
	if (argc == 2) {
		end = va;
	} else {
		end = strtol(argv[2], NULL, 16);
	}

	if (end > 0xfffff000) { // check for overflow
		end = 0xfffff000;
	} else {
		end = ROUNDUP(end, PGSIZE);
	}

	pde_t *pgdir = (pde_t*) PGADDR(PDX(UVPT), PDX(UVPT), 0x0);
	pte_t *pte = NULL;

	while (va <= end) {
		pte = pgdir_walk(pgdir, (void *) va, 0);
		if (pte) {
			cprintf("%08x  %08x  %u  %u  %u\n", va, PTE_ADDR(*pte), 
				(*pte & PTE_U) > 0, (*pte & PTE_W) > 0, (*pte & PTE_P) > 0);
		} else {
			cprintf("%08x  %c          %c  %c  %c\n", va, '-', '-', '-', '-');
		}

		if (va == 0xfffff000) break; // don't let it overflow either

		va += PGSIZE;
	}

	return 0;
}

int
mon_setpgperms(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}
	
	uintptr_t addr = ROUNDDOWN(strtol(argv[1], NULL, 16), PGSIZE); // page addr
	pde_t *pgdir = (pde_t*) PGADDR(PDX(UVPT), PDX(UVPT), 0x0);
	pte_t *pte = pgdir_walk(pgdir, (void *) addr, 0);

	// If page mapping non-existent, return
	if (!pte) {
		cprintf("No page mapping for VA: %p\n", (void *) addr);
		return 0;
	}

	char* bits = argv[2];
	uint8_t flags = 0;
	int i = 0;

	int len = strlen(bits);	
	char b = 0;

	// Only allow changing UWP bits, don't care about rest for now
	for (i = 0; i < 3; i++) {
		if (i < len ) {
			b = bits[len -i - 1];
		} else {
			b = 0; // retain trailing bits
		}		

		switch(b) {
		case '0':
			flags |= (0 << i);
			break;
		case '1':
			flags |= (1 << i);
			break;
		default: // retain old bit otherwise
			flags |= ((*pte) & (1 << i)) > 0; 
			break;			
		}
	}

	// Clear old perm bits and put new ones
	*pte = (*pte & ~0x3) | flags;

	return 0;
}

int
mon_examine(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}

	uintptr_t va = strtol(argv[1], NULL, 16); // start va
	uint32_t length = strtol(argv[2], NULL, 16); // bytes to examine

	pde_t *pgdir = (pde_t*) PGADDR(PDX(UVPT), PDX(UVPT), 0x0);
	pte_t *pte; 

	// Naive for now, just prints byte by byte. Will add more functionality 
	// like reading full words later.
	char* addr = (char *) va;
	while (length > 0) {
		pte = pgdir_walk(pgdir, addr, 0);
		if (!pte) { // maybe a page in the middle is not mapped? skip to next page
			cprintf("%p: %s\n", (void *) addr, "Unmapped address!");
			
			if ((length < PGSIZE) || // no more to print after skipping page
			   ((uintptr_t)addr >= 0xfffff000)) {  // no next page
				cprintf("All remaining addresses are unmapped.\n");
				break;
			}

			uintptr_t oldaddr = (uintptr_t) addr; // skip page and jump to next
			addr = ROUNDUP(addr, PGSIZE); 
			length -= ((uintptr_t)addr - oldaddr);
			
		} else {
			cprintf("%p: %x\n", (void *) addr, (unsigned char) *((char *) addr));
		}

		addr++;
		length--;
	} 
	
	return 0;
}

int
mon_examinep(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}

	physaddr_t pa = strtol(argv[1], NULL, 16); // start pa
	uint32_t length = strtol(argv[2], NULL, 16); // bytes to examine

	pde_t *pgdir = (pde_t*) PGADDR(PDX(UVPT), PDX(UVPT), 0x0);
	pte_t *pte; 

	// Naive for now, just prints byte by byte. Will add more functionality 
	// like reading full words later.
	char* addr = (char *) pa;
	while (length > 0) {
		pte = pgdir_walk(pgdir, page2kva(pa2page((physaddr_t)addr)), 0);
		if (!pte) { // maybe a page in the middle is not mapped? skip to next page
			cprintf("%p: %s\n", (void *) addr, "Unmapped address!");
			
		} else {
			uintptr_t va = (uintptr_t) page2kva(pa2page((physaddr_t)addr)) | PGOFF(addr);
			cprintf("%p: %x\n", (void *) addr, (unsigned char) *((char *) va));
		}

		addr++;
		length--;
	}

	return 0;
}

int
mon_step(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 1) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}

	if (!tf) {
		cprintf("No user environment running to step instruction.\n");	
		return 0;
	}

	tf->tf_eflags |= FL_TF; // Set trapflag in EFLAGS to enable stepping of instrs

	return -1; // need to break from monitor loop
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 1) {
		cprintf("argc: mismatch\n"); 
		return 0;
	}

	if (!tf) {
		cprintf("No user environment running to continue execution.\n");	
		return 0;
	}

	tf->tf_eflags &= ~FL_TF; // Unset trapflag to stop stepping and continue execution

	return -1; // need to break from monitor loop
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
