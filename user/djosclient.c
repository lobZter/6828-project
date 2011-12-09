#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define debug 1

#define PORT 7

#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests

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
        char buffer[BUFFSIZE];

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
			req[0], *((envid_t *) (req + 1)));
	}

	if (write(sock, req, len) != len) {
		die("Failed to send request to server!");
	}
}
void
send_buff(const void *req, int len)
{
	int sock = connect_serv();
	char buffer[BUFFSIZE];

	issue_request(sock, req, len);

	while (1)
        {                                                                       
                cprintf("Waiting for response from server...\n");                  
                // Receive message                                              
                if (read(sock, buffer, BUFFSIZE) < 0)
                        panic("failed to read");
		close(sock);
	       
                cprintf("Received: %d\n", *((int *) buffer));

		if (*((int *) buffer) == -E_FAIL){
			buffer[0] = ABORT;
			*((envid_t *) (buffer + 1)) = *((envid_t *) (req + 1));
			sock = connect_serv();
			issue_request(sock, buffer, 1 + sizeof(envid_t));
			die("Failed to send request");
		}
		else if(*((int *) buffer) == -E_BAD_REQ){
			sock = connect_serv();
			issue_request(sock, req, len);
		}
		else{
			break;
		}
        }                                                               
}

int
send_lease_req(envid_t envid, struct Env *env)
{
	char buffer[BUFFSIZE];
	int r;

	// Clear buffer
	memset(buffer, 0, BUFFSIZE);
	
	buffer[0] = LEASE;
	*((envid_t *)(buffer + 1)) = envid;
	struct Env *e = (struct Env *) (buffer + sizeof(envid_t) + 1);

	memmove(buffer + sizeof(envid_t) + 1, (void *) env,
		sizeof(struct Env));

	e->env_status = ENV_LEASED;

	if (debug){
		cprintf("Sending struct Env: \n"
			"  env_id: %x\n"
			"  env_parent_id: %x\n"
			"  env_status: %x\n"
			"  env_hostip: %x\n",
			e->env_id, e->env_parent_id,
			e->env_status, e->env_hostip);
	}

	return send_buff(buffer, 1 + sizeof(struct Env) + 
			 sizeof(envid_t));
}

void
send_page_req(envid_t envid, uintptr_t va, int perm)
{
	int r, i;
	char buffer[BUFFSIZE];
	char *s;

	buffer[0] = PAGE;
	*((envid_t *) (buffer + 1)) = envid;
	*((uintptr_t *) (buffer + 1 + sizeof(envid_t))) = va;
	*((int *) (buffer + 1 + sizeof(envid_t) + sizeof(uintptr_t))) = perm;

	for (i = 0; i < 4; i++){
		*((int *) (buffer + 1 + sizeof(envid_t) 
			   + sizeof(uintptr_t) + sizeof(int))) = i;
		sys_copy_mem(envid, (void *) (va + i*1024),  
			     (buffer + 1 + sizeof(envid_t) 
			      + sizeof(uintptr_t) + 2 * sizeof(int)));

		if (debug){
			cprintf("Sending page: \n"
				"  env_id: %x\n"
				"  va: %x\n"
				"  chunk: %d\n",
				envid, va,
				i);
		}
		send_buff(buffer, 1 + sizeof(struct Env) + 
			  sizeof(envid_t));
	}
}

void
send_pages(envid_t envid)
{
	uintptr_t addr;
	int i;

	for (addr = UTEXT; addr < UXSTACKTOP - PGSIZE; addr += PGSIZE){
		if(vpd[PDX(addr)] & PTE_P){
			if(vpt[PGNUM(addr)] & PTE_P){
				send_page_req(envid, addr, 
					      PGOFF(vpt[PGNUM(addr)]) & PTE_SYSCALL);
			}
		}
	}
}

void
send_done_request(envid_t envid)
{
	int r;
	char buffer[BUFFSIZE];
	
	buffer[0] = DONE;
	*((envid_t *) (buffer + 1)) = envid;
	send_buff(buffer, 1 + sizeof(envid_t));
}
//For now can only send thisenv
int
send_env(const volatile struct Env *env)
{
	int r;
	uintptr_t addr;
	

	send_lease_req(env->env_id, env);
	send_pages(env->env_id);
	send_done_request(env->env_id);

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
	send_inet_req();
	exit();
}
