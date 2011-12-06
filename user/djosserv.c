#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define debug 1

#define PORT 7

#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests

#define LEASES (sizeof(lease_map)/sizeof(struct lease_entry)) // # of leases

#define LEASE 0
#define PAGE 1
#define DONE_LEASE 2
#define ABORT_LEASE 3

// New error codes (reuse E_NO_MEM)
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202

struct lease_entry {
	envid_t src;
	envid_t dst;
};

struct lease_entry lease_map[] = {
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
};

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
	for (i = 0; i < LEASES; i++) {
		if (lease_map[i].src == src_id) {
			return i;
		}
	}
	return -1;
}

void
destroy_lease(envid_t env_id)
{
	int i;
	i = find_lease(env_id);

	if (i == -1) return;

	// Destroy leased env
	sys_env_destroy(lease_map[i].dst);

	// Clear lease_map entry
	lease_map[i].src = 0;
	lease_map[i].dst = 0;
}

int
process_lease(char *buffer)
{
	int i, entry;
	struct Env req_env;
	envid_t src_id, dst_id;

	// Check if an entry is available in lease map
	entry = -1;
	for (i = 0; i < LEASES; i++) {
		if (!lease_map[i].src) {
			if (lease_map[i].dst) {
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

	if (debug) {
		cprintf("New lease request: \n"
			"  env_id: %x\n"
			"  env_parent_id: %x\n"
			"  env_status: %x\n"
			"  env_hostip: %x\n",
			req_env.env_id, req_env.env_parent_id,
			req_env.env_status, req_env.env_hostip);
	}

	// Env must have status = ENV_LEASED
	if (req_env.env_status != ENV_LEASED) return -E_BAD_REQ;

	// Set parent_id of env to self (doesn't have notion of parent_id
	// anymore
	req_env.env_parent_id = thisenv->env_id;	

	// If there is any free env, copy over request env.
	if (sys_env_lease(&req_env, &dst_id)) {
		return -E_NO_LEASE;
	}

	// Set up mapping in lease map
	lease_map[i].src = req_env.env_id;
	lease_map[i].dst = dst_id;

	if (debug) {
		cprintf("New lease mapped: %x->%x\n",
			lease_map[i].src, lease_map[i].dst);
	}

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

	if (!dst_id) return -E_FAIL;

	// Read va to copy data on. Must be page aligned.
	va = *((uintptr_t *) buffer);
	buffer += sizeof(uintptr_t);
	if (va % PGSIZE) return -E_BAD_REQ;

	// Read perms
	perm = *((uint32_t *) buffer);
	buffer += sizeof(uint32_t);

	// Read *chunk/split* id, 0 <= i <= 3 (four 1024 byte chunks)
	i = *buffer;
	buffer++;
	if (i > 3) return -E_BAD_REQ;

	// Allocate page if first chunk
	if (i == 0) { 
		if ((r = sys_page_alloc(src_id, (void *) va, perm)) < 0) {
			if (r == -E_INVAL) return -E_BAD_REQ;
			if (r == -E_BAD_ENV) return -E_FAIL;
			return -E_NO_MEM;
		}
	}

	// Copy data to page (for now hardcoded to copy 1024 bytes only)
	if (sys_copy_mem(dst_id, (void *) (va + i*1024), buffer) < 0) {
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

	// Check lease map
	if ((i = find_lease(src_id)) < 0) {
		return -E_FAIL;
	}

	if (!lease_map[i].dst) return -E_FAIL;

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
	destroy_lease(src_id);

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

	switch(req_type) {
	case LEASE:
		return process_lease(buffer);
	case PAGE:
		return process_page_req(buffer);
	case DONE_LEASE:
		return process_done_lease(buffer);
	case ABORT_LEASE:
		return process_abort_lease(buffer);
	default:
		return -E_BAD_REQ;
	}

	return 0;
}

void
issue_reply(int sock, int status, envid_t env_id)
{
	// For now only send status code back
	if (debug) {
		cprintf("Sending response: %d, %x\n", status, env_id);
	}

	// If failure occurred, unlease env_id
	if (status == -E_FAIL) {
		destroy_lease(env_id);
	}

	char buf[sizeof(int) + sizeof(envid_t)];
	*(int *) buf = status;
	*(envid_t *) (buf + sizeof(int)) = env_id;
	write(sock, buf, sizeof(int));
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
			die("Failed to receive initial bytes from client");

		// Parse and process request
		r = process_request(buffer);

/*		if (debug) {
			buffer[0] = 0;
			*((envid_t *)(buffer + 1)) = thisenv->env_id;
			struct Env *e = (struct Env *) 
				(buffer + sizeof(envid_t) + 1);
			memmove(buffer + sizeof(envid_t) + 1, (void *) thisenv,
				sizeof(struct Env));
			e->env_status = ENV_LEASED;
			cprintf("Sending struct Env: \n"
				"  env_id: %x\n"
				"  env_parent_id: %x\n"
				"  env_status: %x\n"
				"  env_hostip: %x\n",
				e->env_id, e->env_parent_id,
				e->env_status, e->env_hostip);
			write(sock, buffer, 1 + sizeof(struct Env) + 
			      sizeof(envid_t));
			break;
		}
*/
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

	binaryname = "djosrcv";

	// Set page fault hanlder
	set_pgfault_handler(pg_handler);

	// Create the TCP socket
	if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		die("Failed to create socket");

	cprintf("Opened DJOS Receive Socket\n");

	// Construct the server sockaddr_in structure
	memset(&echoserver, 0, sizeof(echoserver));       // Clear struct
	echoserver.sin_family = AF_INET;                  // Internet/IP
	echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   // IP address
	echoserver.sin_port = htons(PORT);		  // server port

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
		unsigned int clientlen = sizeof(echoclient);
		// Wait for client connection
		if ((clientsock =
		     accept(serversock, (struct sockaddr *) &echoclient,
			    &clientlen)) < 0) {
			die("Failed to accept client connection");
		}

		cprintf("Client connected: Handling...\n");
		handle_client(clientsock);
	}

	close(serversock);
}
