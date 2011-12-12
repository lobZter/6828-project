#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "djos.h"

// Status for struct lease_entry
#define LE_FREE 0
#define LE_BUSY 1
#define LE_DONE 2

#define PTE_COW 0x800

struct lease_entry {
	envid_t src;
	envid_t dst;
	char status;
	int stime;
	void *thisenv;
};

struct lease_entry lease_map[SLEASES];

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

int
find_lease(envid_t src_id) 
{
	int i;
	for (i = 0; i < SLEASES; i++) {
		if (lease_map[i].src == src_id) {
			return i;
		}
	}
	return -1;
}

void
destroy_lease_id(int i)
{
	if (i == -1) return;

	// Destroy leased env
	sys_env_destroy(lease_map[i].dst);

	// Clear lease_map entry
	lease_map[i].src = 0;
	lease_map[i].dst = 0;
	lease_map[i].status = LE_FREE;
	lease_map[i].stime = 0;
	lease_map[i].thisenv = 0;
}

void
destroy_lease(envid_t env_id)
{
	int i;
	i = find_lease(env_id);
	destroy_lease_id(i);
}

void 
gc_lease_map(int ctime) {
	int i;
	envid_t dst;

	if (debug) {
		cprintf("Garbage collecting leases...\n");
	}

	// Check if some lease has not been DONE for too long
	for (i = 0; i < SLEASES; i++) {
		if (!lease_map[i].src) continue;

		if (ctime - lease_map[i].stime > GCTIME &&
		    lease_map[i].status == LE_BUSY) {
			if (debug) {
				cprintf("GCing entry at server %d: %x\n", 
					i, lease_map[i].src);
			}
			destroy_lease_id(i);
		}
	}
}

// Super naive for now
void
check_lease_complete() 
{
	int i;
	struct Env *e;

	if (debug) {
		cprintf("Checking for completed leases...\n");
	}

	for (i = 0; i < SLEASES; i++) {
		if (!lease_map[i].src) continue;

		e = (struct Env *) &envs[ENVX(lease_map[i].dst)];

		// See if env is free by now
		if (lease_map[i].status == LE_DONE) {
			if (e->env_alien != 1 ||
			    e->env_status == ENV_FREE) {
				if (debug) {
					cprintf("GCing completed lease "
						"%d: %x\n",
					i, lease_map[i].src);
				}
				destroy_lease_id(i);
			}
		}
	}
}

int
process_start_lease(char *buffer)
{
	int i, entry;
	struct Env req_env;
	envid_t src_id, dst_id;
	void *tenv;

	// Check if an entry is available in lease map
	entry = -1;
	for (i = 0; i < SLEASES; i++) {
		if (lease_map[i].status == LE_FREE) {
			if (lease_map[i].dst || lease_map[i].src) {
				die("Lease map is inconsistent!");
			}
			entry = i;
			break;
		}
	}
	
	if (entry == -1) return -E_NO_LEASE;

	// Read src id
	src_id = *((envid_t *) buffer);
	buffer += sizeof(envid_t);

	// Read struct Env from request
	req_env = *((struct Env *) buffer);
	buffer += sizeof(struct Env);

	// Read thisenv
	tenv = *((void **) buffer);

	if (debug) {
		cprintf("New lease request: \n"
			"  env_id: %x\n"
			"  env_parent_id: %x\n"
			"  env_status: %x\n"
			"  env_hostip: %x\n",
			req_env.env_id, req_env.env_parent_id,
			req_env.env_status, req_env.env_hostip);
	}

	// Env must have status = ENV_SUSPENDED
	if (req_env.env_status != ENV_SUSPENDED) return -E_BAD_REQ;

	// Set hosteid
	req_env.env_hosteid = src_id;

	// If there is any free env, copy over request env.
	if (sys_env_lease(&req_env, &dst_id)) {
		return -E_NO_LEASE;
	}

	// Set up mapping in lease map
	lease_map[i].src = req_env.env_id;
	lease_map[i].dst = dst_id;
	lease_map[i].status = LE_BUSY;
	lease_map[i].stime = sys_time_msec();
	lease_map[i].thisenv = tenv;

	cprintf("New lease received! Mapped %08x->%08x.\n",
		lease_map[i].src, lease_map[i].dst);

	return 0;
}

int
process_page_req(char *buffer)
{
	int i, perm, r;
	envid_t src_id, dst_id;
	uintptr_t va;

	src_id = *((envid_t *) buffer);
	buffer += sizeof(envid_t);

	// Check lease map
	if ((i = find_lease(src_id)) < 0) {
		return -E_FAIL;
	}
	dst_id = lease_map[i].dst;

	// Read va to copy data on. Must be page aligned.
	va = *((uintptr_t *) buffer);
	buffer += sizeof(uintptr_t);

	// Read perms
	perm = *((uint32_t *) buffer);
	buffer += sizeof(uint32_t);

	// If COW, make W
	if (perm & PTE_COW) {
		perm &= ~PTE_COW;
		perm |= PTE_W;
	}

	// Read *chunk/split* id, 0 <= i <= 3 (four 1024 byte chunks)
	i = *buffer;
	buffer++;

	if (debug) {
		cprintf("New page request: \n"
			"  env_id: %x\n"
			"  va: %x\n"
			"  perm: %x\n"
			"  chunk: %d\n",
			src_id, va, perm, i);

	}

	if (!dst_id) return -E_FAIL;
	if (va % PGSIZE) return -E_BAD_REQ;
	if (i > 3) return -E_BAD_REQ;

	// Allocate page if first chunk
	if (i == 0) { 
		if ((r = sys_page_alloc(dst_id, (void *) va, perm)) < 0) {
			if (r == -E_INVAL) return -E_BAD_REQ;
			if (r == -E_BAD_ENV) return -E_FAIL;
			return -E_NO_MEM;
		}
	}

	// Copy data to page from buff 
        // (for now hardcoded to copy 1024 bytes only)
	if (sys_copy_mem(dst_id, (void *) (va + i*1024), buffer, 
			 perm, 1) < 0) {
		return -E_FAIL;
	}

	return 0;
}

int
process_done_lease(char *buffer)
{
	int i;
	envid_t src_id;

	src_id = *((envid_t *) buffer);

	if (debug) {
		cprintf("New lease done request: \n"
			"  env_id: %x\n",
			src_id);
	}

	// Check lease map
	if ((i = find_lease(src_id)) < 0) {
		return -E_FAIL;
	}

	if (!lease_map[i].dst) return -E_FAIL;
	lease_map[i].status = LE_DONE;

	// Fix thisenv ptr
	sys_env_set_thisenv(lease_map[i].dst, lease_map[i].thisenv);

	// Change status to ENV_RUNNABLE
	// We have transfered all required state so can start executing
	// leased env now.
	if (sys_env_set_status(lease_map[i].dst, ENV_RUNNABLE) < 0) {
		return -E_FAIL;
	}

	return 0;
}

int
process_abort_lease(char *buffer)
{
	int i;
	envid_t src_id;

	// Destroy lease
	src_id = *((envid_t *) buffer);

	if (debug) {
		cprintf("New lease abort request: \n"
			"  env_id: %x\n",
			src_id);
	}

	destroy_lease(src_id);
	if ((i = find_lease(src_id)) >= 0) {
		sys_env_destroy(lease_map[i].dst);
	}

	return 0;
}

int
process_ipc_start(char *buffer)
{
	envid_t dst;
	int r;

	struct ipc_pkt packet = *((struct ipc_pkt *) buffer);

	if (!packet.pkt_fromalien) {
		if ((r = find_lease(packet.pkt_dst)) < 0) {
			return -E_FAIL;
		}
		dst = lease_map[r].dst;
	}
	else {
		dst = packet.pkt_dst;
	}

	if (debug) {
		cprintf("New IPC packet: \n"
			"  src_id: %x\n"
			"  dst_id: %x\n"
			"  local dst: %x\n"
			"  val: %d\n"
			"  fromalien: %d\n",
			packet.pkt_src, packet.pkt_dst, dst, packet.pkt_val,
			packet.pkt_fromalien);
	}
	
	if (!packet.pkt_va) {
		packet.pkt_va = UTOP;
	}

	r = sys_ipc_try_send(dst, packet.pkt_val, (void *) packet.pkt_va, 
			     packet.pkt_perm);

	switch (r) {
	case -E_IPC_NOT_RECV:
		return -E_NO_IPC;
	case -E_INVAL:
		return -E_BAD_REQ;
	case -E_BAD_ENV:
		return -E_FAIL;
	}

	return r;
}
int
process_completed_lease(char *buffer)
{
	int i;
	envid_t envid;
	struct Env *e;

	// Destory env
	envid = *((envid_t *) buffer);

	if (debug) {
		cprintf("New lease completed request: \n"
			"  env_id: %x\n",
			envid);
	}

	cprintf("Process %08x completed!\n", envid);

	e = (struct Env *) &envs[ENVX(envid)];

	if (e->env_status == ENV_LEASED) {
		i = sys_env_destroy(envid);
		if (i < 0) return -E_BAD_REQ;
	}

	return 0;
}

int
process_request(char *buffer)
{
	char req_type;

	if (!buffer) return -E_BAD_REQ;

	req_type = *buffer;
	buffer += 1;

	if (debug) {
		cprintf("Processing request type: %d\n", (int) req_type);
	}

	switch((int)req_type) {
	case PAGE_REQ:
		return process_page_req(buffer);
	case START_LEASE:
		return process_start_lease(buffer);
	case DONE_LEASE:
		return process_done_lease(buffer);
	case ABORT_LEASE:
		return process_abort_lease(buffer);
	case START_IPC:
		return process_ipc_start(buffer);
	case COMPLETED_LEASE:
		return process_completed_lease(buffer);
	default:
		return -E_BAD_REQ;
	}

	return 0;
}

int
issue_reply(int sock, int status, envid_t env_id)
{
	// For now only send status code back
	if (debug) {
		cprintf("Sending response: %d, %x\n", status, env_id);
	}

	uint32_t len = sizeof(int) + sizeof(envid_t);
	char buf[len];
	*(int *) buf = status;
	*(envid_t *) (buf + sizeof(int)) = env_id;

	if (write(sock, buf, len) != len) {
		if (debug)
			cprintf("Failed to send response to client!\n");
		return -1;
	}

	return len;
}

void
handle_client(int sock)
{
	int r;
	char buffer[BUFFSIZE];
	int received = -1;

	// Clear buffer
	memset(buffer, 0, BUFFSIZE);

	while (1)
	{
		// Receive message
		if ((received = read(sock, buffer, BUFFSIZE)) < 0)
			die("Failed to receive initial bytes from client\n");

		// Parse and process request
		r = process_request(buffer);

		// Send reply to request
		issue_reply(sock, r, *((envid_t *)(buffer + 1)));

		// no keep alive
		break;
	}

	close(sock);
}

// Page fault handler
void
pg_handler(struct UTrapframe *utf)
{
	int r;
	void *addr = (void*)utf->utf_fault_va;

	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
}

void
umain(int argc, char **argv)
{
	int serversock, clientsock;
	struct sockaddr_in echoserver, echoclient;
	unsigned int echolen;
	int ltime, ctime;

	binaryname = "djosserv";

	// Set page fault hanlder
	set_pgfault_handler(pg_handler);

	// Clear lease map
	memmove(lease_map, 0, sizeof(struct lease_entry) & SLEASES);

	// Get start time
	ltime = sys_time_msec();

	// Create the TCP socket
	if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		die("Failed to create socket");

	cprintf("Running DJOS Server %x\n", thisenv->env_id);
	cprintf("Opened DJOS Receive Socket\n");

	// Construct the server sockaddr_in structure
	memset(&echoserver, 0, sizeof(echoserver));       // Clear struct
	echoserver.sin_family = AF_INET;                  // Internet/IP
	echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   // IP address
	echoserver.sin_port = htons(SPORT);		  // server port

	cprintf("Binding DJOS Receive Socket\n");

	// Bind the server socket
	if (bind(serversock, (struct sockaddr *) &echoserver,
		 sizeof(echoserver)) < 0) {
		die("Failed to bind the server socket");
	}

	// Listen on the server socket
	if (listen(serversock, MAXPENDING) < 0)
		die("Failed to listen on server socket");

	cprintf("Listening to DJOS Requests\n");

	// Run until canceled
	while (1) {
		// GC?
		ctime = sys_time_msec();
		if (ctime - ltime > GCTIME) {
			ltime = ctime;
			gc_lease_map(ctime);
		}

		// Check if some process done
		check_lease_complete();

		if (debug) {
			cprintf("Waiting for client...\n");
		}
		// Wait for client connection
		unsigned int clientlen = sizeof(echoclient);
		if ((clientsock =
		     accept(serversock, (struct sockaddr *) &echoclient,
			    &clientlen)) < 0) {
			die("Failed to accept client connection");
		}

		// Handle client connection
		if (debug) {
			cprintf("Client connected: Handling...\n");
		}
		handle_client(clientsock);
	}

	close(serversock);
}
