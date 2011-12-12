/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>
#include <user/djos.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, PTE_U);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *e;
	int err;
	
	if ((err = env_alloc(&e, curenv->env_id)) < 0) {
		return err;
	}

	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = curenv->env_tf;
	e->env_tf.tf_regs.reg_eax = 0; // Return val in %eax

	return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env *e;
	
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}

	e->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	struct Env *e;

	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	if ((tf->tf_eip >= UTOP)) {
		return -1;
	}

	e->env_tf = *tf;
	e->env_tf.tf_eflags |= FL_IF;

	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *e;
	
	// Envid valid and caller has perms to access it
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	e->env_pgfault_upcall = func;
	user_mem_assert(e, func, 4, 0);

	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env *e;
	struct Page *pp;
	
	// Envid valid and caller has perms to access it
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	// VA below UTOP and page aligned
	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE) {
		return -E_INVAL;
	}

	// PTE_U | PTE_P must be set
	if ((perm & PTE_U) == 0 || (perm & PTE_P) == 0) {
		return -E_INVAL;
	}

	// Only U, P, W and AVAIL can be set
	if ((perm & ~(PTE_U | PTE_P | PTE_W | PTE_AVAIL)) != 0) {
		return -E_INVAL;
	}

	if ((pp = page_alloc(ALLOC_ZERO)) == NULL) {
		return -E_NO_MEM;
	}

	if (page_insert(e->env_pgdir, pp, va, perm) < 0) {
		page_free(pp);
		return -E_NO_MEM;
	}

	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *srcenv;
	struct Env *dstenv;
	pte_t *pte;
	struct Page *pp;

	// Env Ids valid and caller has perms to access them
	if (envid2env(srcenvid, &srcenv, 1) < 0 || 
		envid2env(dstenvid, &dstenv, 1) < 0) {
		return -E_BAD_ENV;
	}
		
	// VAs below UTOP and page aligned
	if ((uintptr_t)srcva >= UTOP || (uintptr_t)srcva % PGSIZE ||
		(uintptr_t)dstva >= UTOP || (uintptr_t)dstva % PGSIZE) {
		return -E_INVAL;
	}

	if ((pp = page_lookup(srcenv->env_pgdir, srcva, &pte)) == NULL) {
		return -E_INVAL;
	}

	// PTE_U | PTE_P must be set
	if ((perm & PTE_U) == 0 || (perm & PTE_P) == 0) {
		return -E_INVAL;
	}

	// Only U, P, W and AVAIL can be set
	if ((perm & ~(PTE_U | PTE_P | PTE_W | PTE_AVAIL)) != 0) {
		return -E_INVAL;
	}

	// Dest page writable but source isn't
	if ((perm & PTE_W) && ((*pte & PTE_W) == 0)) {
		return -E_INVAL;
	}

	if (page_insert(dstenv->env_pgdir, pp, dstva, perm) < 0) {
		return -E_NO_MEM;
	}

	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env *e;
	
	// Env Id valid and caller has perms to change it
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	// VA below UTOP and page aligned
	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE) {
		return -E_INVAL;
	}

	page_remove(e->env_pgdir, va);

	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *rcv;
	pte_t *pte;
	struct Page *pp;

	envid_t jdos_client = 0;
	struct Env *e;
	int i, r;

	if (curenv->env_alien && 
	     ((curenv->env_hosteid & 0xfff00000) == 
	      (envid & 0xfff00000))) {
		goto djos_send;
	}

	// Is receiver valid?
	if (envid2env(envid, &rcv, 0) < 0) {
		return -E_BAD_ENV;
	}

	if (rcv->env_status == ENV_SUSPENDED) {
		return -E_IPC_NOT_RECV;
	}

	if (rcv->env_status == ENV_LEASED) { // is leased?
	djos_send:
		for (i = 0; i < NENV; i++) {
			if (envs[i].env_type == ENV_TYPE_JDOSC) {
				jdos_client = envs[i].env_id;
				break;
			}
		}

		// jdos client running?
		if (!jdos_client) return -E_BAD_ENV; 

		if ((r = envid2env(jdos_client, &e, 0)) < 0) return r;

		// Mark suspended and try to send ipc
		curenv->env_status = ENV_SUSPENDED; 

		sys_page_alloc(curenv->env_id, (void *) IPCSND, 
			       PTE_U|PTE_P|PTE_W);

		*((envid_t *) IPCSND) = envid;
		*((uint32_t *)(IPCSND + sizeof(envid_t))) = value;
		*((unsigned *)(IPCSND + sizeof(envid_t) +
				   sizeof(uint32_t))) = perm;

		//can't write to page
		r = sys_ipc_try_send(jdos_client, CLIENT_SEND_IPC, 
				     (void *) IPCSND, PTE_U|PTE_P); 

		sys_page_unmap(curenv->env_id, (void *) IPCSND);

		// Failed to send ipc, back to running!
		if (r < 0) {
			cprintf("sys_send_ipc: failed to send ipc %d\n", r);
			curenv->env_status = ENV_RUNNABLE;
			return r;
		}
	}
	else {
		// Is receiver waiting?
		if (!rcv->env_ipc_recving) {
			return -E_IPC_NOT_RECV;
		}
		
		// Try mapping page from sender to receiver (if receiver 
		// wants it, and sender wants to send it)
		// NOTE: Can't use sys_map_page as it checks for env perms
		if ((uint32_t) rcv->env_ipc_dstva < UTOP && 
		    (uint32_t) srcva < UTOP) {
			if (!(pp = page_lookup(curenv->env_pgdir, 
					       srcva, &pte)))
				return -E_INVAL;
			
			if ((perm & PTE_W) && !(*pte & PTE_W))
				return -E_INVAL;
			
			if (page_insert(rcv->env_pgdir, pp, 
					rcv->env_ipc_dstva, perm) < 0)
				return -E_NO_MEM;
		}
		
		// Set fields which mark receiver as not waiting
		rcv->env_ipc_recving = 0;
		rcv->env_ipc_dstva = (void *) UTOP; // invalid dstva
		
		// Set received data fields of receiver
		rcv->env_ipc_value = value;
		rcv->env_ipc_from = curenv->env_id;	
		rcv->env_ipc_perm = perm;
		
		// Mark receiver as RUNNABLE
		rcv->env_status = ENV_RUNNABLE;
	}
	
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	if ((uintptr_t) dstva < UTOP && ((uintptr_t) dstva % PGSIZE)) {
		return -E_INVAL;
	}

	// Set fields which mark as waiting
	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;

	// Reset previous received data fields
	curenv->env_ipc_value = 0;
	curenv->env_ipc_from = 0;	
	curenv->env_ipc_perm = 0;
	
	// Mark as NOT_RUNNABLE (waiting)
	curenv->env_status = ENV_NOT_RUNNABLE;

	return 0;
}

static int
sys_env_swap(envid_t envid) 
{
	struct Env* e;

	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	if (e->env_status != ENV_NOT_RUNNABLE) {
		return -E_BAD_ENV;
	}

	struct Env temp = *curenv;
	curenv->env_tf = e->env_tf;	
	curenv->env_pgdir = e->env_pgdir;
	lcr3(PADDR(curenv->env_pgdir));
	
	// Need to do this to free old pgdir
	e->env_pgdir = temp.env_pgdir;
	e->env_tf = temp.env_tf;
	env_destroy(e);

	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	return time_msec();
}

// Try to send packet over network
static int
sys_net_try_send(char *data, int len)
{
	if ((uintptr_t)data >= UTOP) {
		return -E_INVAL;
	}

	return e1000_transmit(data, len);
}

// Try to receive packet over network
static int
sys_net_try_receive(char *data, int *len)
{
	if ((uintptr_t)data >= UTOP) {
		return -E_INVAL;
	}

	*len = e1000_receive(data);
	if (*len > 0) {
		return 0;
	}
	
	return *len;
}

static int
sys_get_mac(uint32_t *low, uint32_t *high)
{
	*low = e1000[E1000_RAL];
	*high = e1000[E1000_RAH] & 0xffff;

	return 0;
}

/* DJOS syscalls */

static int // server call to create new lease env
sys_env_lease(struct Env *src, envid_t *dst_id) 
{
	int r;
	struct Env* e;

	if ((r = env_alloc(&e, src->env_parent_id)) < 0) {
		return r;
	}

	/* No need to copy link, id, cpunum, pgdir */

	e->env_tf = src->env_tf;
	e->env_parent_id = src->env_parent_id;

	e->env_status = src->env_status;
	e->env_type = src->env_type;
	e->env_runs = src->env_runs;

	e->env_pgfault_upcall = src->env_pgfault_upcall;

	e->env_ipc_recving = src->env_ipc_recving;
	e->env_ipc_dstva = src->env_ipc_dstva;
	e->env_ipc_value = src->env_ipc_value;
	e->env_ipc_from = src->env_ipc_from;
	e->env_ipc_perm = src->env_ipc_perm;

	e->env_hostip = src->env_hostip;
	e->env_alien = 1; // Mark as alien
	e->env_hostport = src->env_hostport;
	e->env_hosteid = src->env_hosteid;

	*dst_id = e->env_id;

	return 0;
}

int // Only copies 1024 bytes! server and client call
sys_copy_mem(envid_t env_id, void* addr, void* buf, int perm, bool frombuf)
{
	void *pgva = (void *) ROUNDDOWN(addr, PGSIZE);

	if (sys_page_map(env_id, pgva, curenv->env_id, (void *) UTEMP, 
			 perm) < 0) 
		return -E_INVAL;

	if (frombuf) {
		memmove((void *) (UTEMP + PGOFF(addr)), buf, 1024);
	}
	else {
		memmove(buf, (void *) (UTEMP + PGOFF(addr)), 1024);
	}

	if (sys_page_unmap(curenv->env_id, (void *) UTEMP) < 0)
		return -E_INVAL;

	return 0;
}

int // server call to get perms
sys_get_perms(envid_t envid, void *va, int *perm) 
{
	struct Env *e;
	pte_t *pte;
	int r;

	if ((uintptr_t) va % PGSIZE) return -E_INVAL;
	
	if ((r = envid2env(envid, &e, 0)) < 0) {
		return r;
	}

	if (!page_lookup(e->env_pgdir, va, &pte)) {
		return -E_INVAL;
	}

	*perm = *pte & PTE_SYSCALL;

	return 0;
}

int // client call on lease failure
sys_env_unsuspend(envid_t envid, uint32_t status, uint32_t value)
{
	struct Env *e;
	int r;

	if ((r = envid2env(envid, &e, 0)) < 0) {
		return r;   
	}

	e->env_tf.tf_regs.reg_eax = value;
	e->env_status = status;

	return 0;
}

int // user call to lease self
sys_migrate(void *thisenv)
{
	envid_t jdos_client = 0;
	struct Env *e;
	int i, r;

	for (i = 0; i < NENV; i++) {
		if (envs[i].env_type == ENV_TYPE_JDOSC) {
			jdos_client = envs[i].env_id;
			break;
		}
	}

	// jdos client running?
	if (!jdos_client) return -E_BAD_ENV; 

	if ((r = envid2env(jdos_client, &e, 0)) < 0) return r;

	// Mark leased and try to migrate
	curenv->env_status = ENV_SUSPENDED; 
	sys_page_alloc(curenv->env_id, (void *) IPCSND, PTE_U|PTE_P|PTE_W);
	*((envid_t *) IPCSND) = curenv->env_id;
	*((void **)(IPCSND + sizeof(envid_t))) = thisenv;

	//can't write to page
	r = sys_ipc_try_send(jdos_client, CLIENT_LEASE_REQUEST, 
			     (void *) IPCSND, PTE_U|PTE_P); 

	sys_page_unmap(curenv->env_id, (void *) IPCSND);

	// Failed to migrate, back to running!
	if (r < 0) {
		cprintf("==> sys_migrate: failed to send ipc %d\n", r);
		curenv->env_status = ENV_RUNNABLE;
		return r;
	}

	// Migrated! BOOM!
	return 0;
}

int
sys_lease_complete() 
{
	envid_t jdos_client = 0;
	struct Env *e;
	int i, r;

	for (i = 0; i < NENV; i++) {
		if (envs[i].env_type == ENV_TYPE_JDOSC) {
			jdos_client = envs[i].env_id;
			break;
		}
	}

	// jdos client running?
	if (!jdos_client) return -E_BAD_ENV; 

	if ((r = envid2env(jdos_client, &e, 0)) < 0) return r;

	// Mark suspended and send lease complete request
	curenv->env_status = ENV_SUSPENDED; 
	sys_page_alloc(curenv->env_id, (void *) IPCSND, PTE_U|PTE_P|PTE_W);
	*((envid_t *) IPCSND) = curenv->env_id;

	// Can't write to page
	r = sys_ipc_try_send(jdos_client, CLIENT_LEASE_COMPLETED, 
			     (void *) IPCSND, PTE_U|PTE_P); 

	sys_page_unmap(curenv->env_id, (void *) IPCSND);

	// Failed to migrate, back to running!
	if (r < 0) {
		cprintf("sys_lease_completed: failed to send ipc %d\n", r);
		curenv->env_status = ENV_RUNNABLE;
		return r;
	}

	return 0;	
}

int
sys_env_set_thisenv(envid_t envid, void *thisenv)
{
	void *pgva = (void *) ROUNDDOWN(thisenv, PGSIZE);

	if (sys_page_map(envid, pgva, curenv->env_id, (void *) UTEMP, 
			 PTE_P|PTE_U|PTE_W) < 0) 
		return -E_INVAL;

	*((struct Env **)(UTEMP + PGOFF(thisenv))) = 
		&((struct Env *)UENVS)[ENVX(envid)];

	if (sys_page_unmap(curenv->env_id, (void *) UTEMP) < 0)
		return -E_INVAL;

	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	switch(syscallno) {
	case SYS_cputs:
		sys_cputs((char *) a1, (size_t) a2);
		break;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		return sys_getenvid();
	case SYS_env_destroy:
		return sys_env_destroy((envid_t) a1);
	case SYS_yield:
		sys_yield();
		break;
	case SYS_exofork:
		return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status((envid_t) a1, (int) a2);
	case SYS_env_set_trapframe:
		return sys_env_set_trapframe((envid_t) a1, (struct Trapframe *) a2);
	case SYS_page_alloc:
		return sys_page_alloc((envid_t) a1, (void *) a2, (int) a3);
	case SYS_page_map:
		return sys_page_map((envid_t) a1, (void *) a2,
				(envid_t) a3, (void *) a4, (int) a5);
	case SYS_page_unmap:
		return sys_page_unmap((envid_t) a1, (void *) a2);
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall((envid_t) a1, (void *) a2);
	case SYS_ipc_try_send:
		return sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void *) a3, (unsigned) a4);
	case SYS_ipc_recv:
		return sys_ipc_recv((void *) a1);
	case SYS_env_swap:
		return sys_env_swap((envid_t) a1);
	case SYS_time_msec:
		return sys_time_msec();
	case SYS_net_try_send:
		return sys_net_try_send((char *) a1, (int) a2);
	case SYS_net_try_receive:
		return sys_net_try_receive((char *) a1, (int *) a2);
	case SYS_get_mac:
		return sys_get_mac((uint32_t *) a1, (uint32_t *) a2);
	case SYS_env_lease:
		return sys_env_lease((struct Env*) a1, (envid_t *) a2);
	case SYS_copy_mem:
		return sys_copy_mem((envid_t) a1, (void *) a2, (void *) a3, 
				    (int) a4, (bool) a5);
	case SYS_get_perms:
		return sys_get_perms((envid_t) a1, (void *) a2, (int *) a3);
	case SYS_env_unsuspend:
		return sys_env_unsuspend((envid_t) a1, (uint32_t) a2, (uint32_t) a3);
	case SYS_env_set_thisenv:
		return sys_env_set_thisenv((envid_t) a1, (void *) a2);
	case SYS_migrate:
		return sys_migrate((void *) a1);
	case SYS_lease_complete:
		return sys_lease_complete();
	default:
		return -E_INVAL;
	}

	return 0; // for syscall that return void
}

