#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define debug 1

#define PORT 7

#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests
#define RETRIES 5       // # of retries

#define LEASE 1
#define PAGE 0
#define DONE 2
#define ABORT 3

// New error codes (reuse E_NO_MEM)
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202

//Client error codes
#define E_REQ_FAILED 300

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
connect_serv()
{
        int r;
        int clientsock;
	struct sockaddr_in client;

        if ((clientsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                die("Doomed!");

        memset(&client, 0, sizeof(client));             // Clear struct
        client.sin_family = AF_INET;                    // Internet/IP
        client.sin_addr.s_addr = htonl(0x12bb0048);     // 18.187.0.72
//        client.sin_addr.s_addr = htonl(0x12b500e8);     // 18.181.0.232 linerva
//        client.sin_addr.s_addr = htonl(0x7f000001);     // 127.0.0.1 localhost
//        client.sin_addr.s_addr = htonl(0x0a00020f);     // 10.0.2.15
        client.sin_port = htons(26591);                    // client port

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
	int sock = connect_serv();
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

			sock = connect_serv();
			issue_request(sock, req, len);
			continue; // retry
		}
		
		return 0;
        }
	
	return 0;
}

int
send_lease_req(envid_t envid, const volatile struct Env *env)
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

	// e->env_status = ENV_LEASED;

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
		sys_copy_mem(envid, (void *) (va + i*1024),  
			     (buffer + 1 + offset));
//		memmove(buffer + 1 + offset, (void *) (va + i*1024), 1024);

		if (debug){
			cprintf("Sending page: \n"
				"  env_id: %x\n"
				"  va: %x\n"
				"  chunk: %d\n",
				envid, va,
				i);
		}

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

	for (addr = UTEXT; addr < UXSTACKTOP - PGSIZE; addr += PGSIZE){
		// add sys call which return perms
		// perm = sys_get_perms(envid, addr);

		if(vpd[PDX(addr)] & PTE_P){
			if(vpt[PGNUM(addr)] & PTE_P){
				r = send_page_req(envid, addr, 
						  PGOFF(vpt[PGNUM(addr)]) & 
						  PTE_SYSCALL);
				if (r < 0) return r;
			}
		}
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
send_env(const volatile struct Env *env)
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

	return 0;
}

void                                                                           
send_inet_req()
{
	send_env(thisenv);
}

void
test(){
	send_lease_req(thisenv->env_id, thisenv);
}
void
umain(int argc, char **argv)
{
	
	set_pgfault_handler(pg_handler);
	send_inet_req();
	cprintf("here nbiatch asdfsfadfdadf\n");
	exit();
}
