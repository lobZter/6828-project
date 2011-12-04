// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if ((err & FEC_WR) == 0) {
		panic("pgfault: page fault was not caused by write; %x.\n", 
		      utf->utf_fault_va);
	}

	if ((vpt[PGNUM(addr)] & PTE_COW) == 0) {
		panic("pgfault: page fault on page which is not COW %x.\n", 
		      utf->utf_fault_va);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	envid_t envid = sys_getenvid();
	
	// Allocate temp page
	if (sys_page_alloc(envid, PFTEMP, PTE_U | PTE_P | PTE_W) < 0) {
		panic("pgfault: can't allocate temp page.\n");
	}

	// Copy data
	memmove(PFTEMP, (void *) ROUNDDOWN(addr, PGSIZE), PGSIZE);

	// Map temp page to old page w/ PTE_W perms
	if (sys_page_map(envid, PFTEMP, envid, (void *) ROUNDDOWN(addr, PGSIZE), 
			PTE_U | PTE_P | PTE_W) < 0) {
		panic("pgfault: can't map temp page to old page.\n");
	}

	// Unmap temp page
	if (sys_page_unmap(envid, PFTEMP) < 0) {
		panic("pgfault: couldn't unmap temp page.\n");
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	envid_t myenvid = sys_getenvid();
	pte_t pte = vpt[pn];
	int perm;

	// If PTE_SHARE, copy mapping directly
	if (pte & PTE_SHARE) {
		if ((r = sys_page_map(myenvid, (void *) (pn * PGSIZE), envid, 
				      (void *) (pn * PGSIZE), 
				      pte & PTE_SYSCALL)) < 0) {
			return r;
		}
	}
	else {
		perm = PTE_U | PTE_P;
		if (pte & PTE_W || pte & PTE_COW) {
			perm |= PTE_COW;
		}

		// Map to envid VA
		if ((r = sys_page_map(myenvid, (void *) (pn * PGSIZE), envid, 
				      (void *) (pn * PGSIZE), perm)) < 0) {
			return r;
		}

		// If COW remap to self
		if (perm & PTE_COW) {
			if ((r = sys_page_map(myenvid, (void *) (pn * PGSIZE), 
					      myenvid,
					      (void *) (pn * PGSIZE), 
					      perm)) < 0) {
				return r;
			}
		}
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.	
	extern void _pgfault_upcall(void);
	envid_t myenvid = sys_getenvid();
	envid_t envid;
	uint32_t i, j, pn;

	// Set page fault handler
	set_pgfault_handler(pgfault);
	
	// Create a child
	if ((envid = sys_exofork()) < 0) {
		return -1;
	}

	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return envid; // in child env? return simply
	}

	// Copy address space to child
	for (i = PDX(UTEXT); i < PDX(UXSTACKTOP); i++) {
		if (vpd[i] & PTE_P) { // If page table present
			for (j = 0; j < NPTENTRIES; j++) {			
				pn = PGNUM(PGADDR(i, j, 0));
				if (pn == PGNUM(UXSTACKTOP - PGSIZE)) {
					break; // Don't map when reach uxstack
				}

				if (vpt[pn] & PTE_P) {
					duppage(envid, pn);
				}			
			}
		}
	}

	// Allocate new exception stack for child	
	if (sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W) < 0) {
		return -1;
	}

	// Map child uxstack to temp page 
	if (sys_page_map(envid, (void *) (UXSTACKTOP - PGSIZE), myenvid, PFTEMP, 
			PTE_U | PTE_P | PTE_W) < 0) {
		return -1;
	}

	// Copy own uxstack to temp page
	memmove((void *)(UXSTACKTOP - PGSIZE), PFTEMP, PGSIZE);

	// Unmap temp page
	if (sys_page_unmap(myenvid, PFTEMP) < 0) {
		return -1;
	}	

	// Set page fault handler in child
	if (sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0) {
		return -1;
	}

	// Mark child env as RUNNABLE
	if (sys_env_set_status(envid, ENV_RUNNABLE) < 0) {
		return -1;
	}

	return envid;
}

// Challenge!
envid_t
sfork(void)
{
	// LAB 4: Your code here.	
	extern void _pgfault_upcall(void);
	envid_t myenvid = sys_getenvid();
	envid_t envid;
	uint32_t i, j, pn;
	int perm;

	// Set page fault handler
	set_pgfault_handler(pgfault);
	
	// Create a child
	if ((envid = sys_exofork()) < 0) {
		return -1;
	}

	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return envid; // in child env? return simply
	}

	// Copy address space to child
	for (i = PDX(UTEXT); i < PDX(UXSTACKTOP); i++) {
		if (vpd[i] & PTE_P) { // If page table present
			for (j = 0; j < NPTENTRIES; j++) {			
				pn = PGNUM(PGADDR(i, j, 0));
				if (pn == PGNUM(UXSTACKTOP - PGSIZE)) {
					break; // Don't map when reach uxstack
				}

				if (pn == PGNUM(USTACKTOP - PGSIZE)) {
					duppage(envid, pn); // COW for stack page
					continue;
				}

				// Map same page to child env with same perms
				if (vpt[pn] & PTE_P) {
					perm = vpt[pn] & ~(vpt[pn] & ~(PTE_P | PTE_U | PTE_W | PTE_AVAIL));
					if (sys_page_map(myenvid, (void *) (PGADDR(i, j, 0)),
						envid, (void *) (PGADDR(i, j, 0)), perm) < 0) {			
						return -1;
					}
				}		
			}
		}
	}

	// Allocate new exception stack for child	
	if (sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W) < 0) {
		return -1;
	}

	// Map child uxstack to temp page 
	if (sys_page_map(envid, (void *) (UXSTACKTOP - PGSIZE), myenvid, PFTEMP, 
			PTE_U | PTE_P | PTE_W) < 0) {
		return -1;
	}

	// Copy own uxstack to temp page
	memmove((void *)(UXSTACKTOP - PGSIZE), PFTEMP, PGSIZE);

	// Unmap temp page
	if (sys_page_unmap(myenvid, PFTEMP) < 0) {
		return -1;
	}	

	// Set page fault handler in child
	if (sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0) {
		return -1;
	}

	// Mark child env as RUNNABLE
	if (sys_env_set_status(envid, ENV_RUNNABLE) < 0) {
		return -1;
	}

	return envid;
}
