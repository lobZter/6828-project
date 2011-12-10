#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define debug 1

#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests
#define RETRIES 0       // # of retries
#define CLEASES 0       // # of client leases
#define MYIP 0x7f000001 // 127.0.0.1
#define MYPORT 8
#define IPCRCV (UTEMP + PGSIZE) // page to map receive 

#define SERVIP 0x12380022 // Server ip
#define SERVPORT 26591    // Server port

#define LEASE 1
#define PAGE 0
#define DONE 2
#define ABORT 3

// New error codes (reuse E_NO_MEM)
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202

struct lease_entry {
	envid_t env_id;
	uint32_t lessee_ip;
};

struct lease_entry lease_map[CLEASES];

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

// Page fault handler
void
pg_handler(struct UTrapframe *utf)
{
	int r;
	void *addr = (void*)utf->utf_fault_va;

	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("Allocating at %x in page fault handler: %e", addr, r);
}

int
connect_serv(uint32_t ip, uint32_t port)
{
        int r;
        int clientsock;
	struct sockaddr_in client;

        if ((clientsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                die("Doomed!");

	/* In this context client is actually DJOS server */

        memset(&client, 0, sizeof(client));             // Clear struct
        client.sin_family = AF_INET;                    // Internet/IP
        client.sin_addr.s_addr = htonl(ip);             // client ip
        client.sin_port = htons(port);                  // client port

        cprintf("Connecting to server at %x...\n", 0x12bb0048);

        if ((r = connect(clientsock, (struct sockaddr *) &client,
                         sizeof(client))) < 0)               
                die("Connection to server failed!");

	return clientsock;
}
void
issue_request(int sock, const void *req, int len)
{
	// For now only send status code back
	if (debug) {
		cprintf("Sending request: %d, %x\n", 
			((char *)req)[0], *((envid_t *) (req + 1)));
	}

	if (write(sock, req, len) != len) {
		die("Failed to send request to server!");
	}
}

int
send_buff(const void *req, int len)
{
	int cretry = 0;
	int sock = connect_serv(SERVIP, SERVPORT);
	char buffer[BUFFSIZE];
	issue_request(sock, req, len);

	while (1)
        {
		cretry++;

		cprintf("Waiting for response from server...\n");   
                
                // Receive message
                if (read(sock, buffer, BUFFSIZE) < 0)
                        panic("Failed to read");
		close(sock);
	       
                cprintf("Received: %d\n", *((int *) buffer));

		if (*((int *) buffer) == -E_FAIL) {
			// server has already destroyed our lease
			return -E_FAIL; 
		}
		else if (*((int *) buffer) == -E_BAD_REQ) {
			if (cretry > RETRIES) {
				return -E_FAIL;
			}

			sock = connect_serv(SERVIP, SERVPORT);
			issue_request(sock, req, len);
			continue; // retry
		}
		
		return 0;
        }
	
	return 0;
}

int
send_lease_req(envid_t envid, struct Env *env)
{
	char buffer[BUFFSIZE];
	int r;
	struct Env *e;

	// Clear buffer
	memset(buffer, 0, BUFFSIZE);
	
	*((char *) buffer) = LEASE;
	*((envid_t *)(buffer + 1)) = envid;
	e = (struct Env *) (buffer + sizeof(envid_t) + 1);

	memmove(e, (void *) env, sizeof(struct Env));

	if (debug){
		cprintf("Sending struct Env: \n"
			"  env_id: %x\n"
			"  env_parent_id: %x\n"
			"  env_status: %x\n"
			"  env_hostip: %x\n",
			e->env_id, e->env_parent_id,
			e->env_status, e->env_hostip);
	}
	
	return send_buff(buffer, 1 + sizeof(struct Env) + sizeof(envid_t));
}

int
send_page_req(envid_t envid, uintptr_t va, int perm)
{
	int r, i, offset;
	char buffer[BUFFSIZE];
	char *s;

	offset = 0;

	* (char *) buffer = PAGE;
	offset++;

	*((envid_t *) (buffer + offset)) = envid;
	offset += sizeof(envid_t);

	*((uintptr_t *) (buffer + offset)) = va;
	offset += sizeof(uintptr_t);

	*((int *) (buffer + offset)) = perm;
	offset += sizeof(int);

	for (i = 0; i < 4; i++) {
		*((char *) (buffer + offset)) = i;

		if (debug){
			cprintf("Sending page: \n"
				"  env_id: %x\n"
				"  va: %x\n"
				"  chunk: %d\n",
				envid, va,
				i);
		}
		
		// Copy to buffer
		r = sys_copy_mem(envid, (void *) (va + i*1024),  
				 (buffer + 1 + offset), perm, 0);
		if (r < 0) return r;

		r = send_buff(buffer, offset + 1025);
		if (r < 0) return r;
	}

	return 0;
}

int
send_pages(envid_t envid)
{
	uintptr_t addr;
	int r, perm;

	for (addr = UTEXT; addr < UTOP; addr += PGSIZE){
		if (sys_get_perms(envid, (void *) addr, &perm) < 0) {
			continue;
		};

		r = send_page_req(envid, addr, perm);
		if (r < 0) return r;
	}

	return 0;
}

int
send_done_request(envid_t envid)
{
	char buffer[BUFFSIZE];
	
	buffer[0] = DONE;
	*((envid_t *) (buffer + 1)) = envid;
	return send_buff(buffer, 1 + sizeof(envid_t));
}

int
send_abort_request(envid_t envid) 
{
	char buffer[BUFFSIZE];

	buffer[0] = ABORT;
	*((envid_t *) (buffer + 1)) = envid;
	return send_buff(buffer, 1 + sizeof(envid_t));
}

int
send_env(struct Env *env)
{
	int r, cretry = 0;
	uintptr_t addr;
	
	while (cretry <= RETRIES) {
		cretry++;

		r = send_lease_req(env->env_id, env);
		if (r < 0) {
			send_abort_request(env->env_id);
			continue;
		}
		
		r = send_pages(env->env_id);
		if (r < 0) {
			send_abort_request(env->env_id);
			continue;
		}

		r = send_done_request(env->env_id);
		if (r < 0) {
			send_abort_request(env->env_id);
			continue;
		}
	}

	if (cretry > RETRIES + 1) return -E_FAIL;

	return 0;
}

void
umain(int argc, char **argv)
{
	struct Env e;
	envid_t envid;
	int r;

	// Set page fault handler
	set_pgfault_handler(pg_handler);

	while (1) {
		cprintf("Waiting for new lease requests...\n");

		// Wait for lease/migrate requests via IPC
		sys_ipc_recv((void *) IPCRCV);

		// Get envid from ipc *value*
		envid = (envid_t) thisenv->env_ipc_value;
		memmove((void *) &e, (void *) &envs[ENVX(envid)], 
			sizeof(struct Env));

		// Ids must match
		if (e.env_id != envid) {
			die("Env id mismatch!");
		}

		// Status must be ENV_LEASED
		if (e.env_status != ENV_LEASED) {
			cprintf("Failed to lease envid %x. Not leased!\n", 
				envid);
		}

		// Set eax to 0, to appear migrate call succeed
		e.env_tf.tf_regs.reg_eax = 0;
		e.env_hostip = MYIP;

		// Try sending env
		r = send_env(&e);

		// If lease failed, then set eax to -1 to indicate failure
		// And mark ENV_RUNNABLE
		if (r < 0) {
			cprintf("Lease to server failed! Aborting...\n");
			sys_env_mark_runnable(envid);
		}
		else {
			// Do what?
		}
	}
}
