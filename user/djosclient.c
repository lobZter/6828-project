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
#define DONE 2

// New error codes (reuse E_NO_MEM)
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202
//Client error codes
#define E_REQ_FAILED 300

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
int
send_lease_req(int sock, envidt_t envid, struct Env env)
{
	int r;
	char buffer[BUFFSIZE];

	// Clear buffer
	memset(buffer, 0, BUFFSIZE);
	
	;
	*((envid_t *)(buffer + 1)) = envid;
	struct Env *e = (struct Env *) 
		(buffer + sizeof(envid_t) + 1);
	memmove(buffer + sizeof(envid_t) + 1, (void *) thisenv,
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
	if(write(sock, buffer, 1 + sizeof(struct Env) + 
		 sizeof(envid_t)) < 0)
		return -E_REQ_FAILED;
	return receive_reply(int sock);
}


int
send_page_req(int sock, envid_t envid, uintptr_t va, int perm)
{
	int r, i;
	char buffer[BUFFSIZE];

	buffer[0] = 1;
	*((envid_t *) (buffer + 1)) = envid;
	*((uintptr_t *) (buffer + 1 + sizeof(envid_t))) = va;
	*((int *) (buffer + 1 + sizeof(envid_t) + sizeof(uintptr_t))) = perm;

	for (i = 0; i < 3; i++){
		*((int *) (buffer + 1 + sizeof(envid_t) 
			   + sizeof(uintptr_t) + sizeof(int))) = i;
		memmove((void *) (buffer + 1 + sizeof(envid_t) 
				  + sizeof(uintptr_t) + 2 * sizeof(int)), 
			(void *) (va + i * 1024), 1024);
		if(write(sock, buffer, 1 + sizeof(struct Env) + 
			 sizeof(envid_t)) < 0)
			return -E_REQ_FAILED;
		if(receive_reply(int sock) < 0)
			return -E_REQ_FAILED; //change to retry maybe?
	}
	return 0;
}

int
send_pages(int sock, envid_t envid)
{
	uintptr_t addr;
	int i;

	for (addr = UTEXT; addr < UXSTACKTOP - PGSIZE; addr += PGSIZE){
		if(vpd[PDX(addr)] & PTE_P){
			if(vpt[PGNUM(addr)] & PTE_P){
				send_page_req(sock, envid, addr, 
					      PGOFF(vpt[PGNUM(addr)]));
			}
		}
	}
}

int
send_done_request(int sock, envid_t envid)
{
	int r;
	char buffer[BUFFSIZE];
	
	buffer[0] = 2;
	*((envid_t *) (buffer + 1)) = envid;
	if(write(sock, buffer, 1 + sizeof(envid_t)) < 0)
		return -E_REQ_FAILED;
	return 0;
}
//For now can only send thisenv
int
send_env(int sock, struct Env *env)
{
	int r;
	uintptr_t addr;
	

	r = send_lease_req(sock, env->env_id, *env);
	if (r < 0)
		continue; //TODO	

	r = send_pages(sock, env->env_id);

	if (r < 0)
		continue;
	send_done_request(sock, env->env_id);

}

void                                                                           
send_inet_req()                                                               
{                                                                               
        int r;                                                                  
        int clientsock;                                                         
        struct sockaddr_in client;                                              
        char buffer[BUFFSIZE];                                                  
                                                                                
        if ((clientsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)       
                die("Doomed!");                                                 
                                                                                
        memset(&client, 0, sizeof(client));             // Clear struct
        client.sin_family = AF_INET;                    // Internet/IP
        client.sin_addr.s_addr = htonl(0x7f000001);     // 18.9.22.69
        client.sin_port = htons(26001);                    // client port

        cprintf("Connecting to MIT...\n");                                      
                                                                                
        if ((r = connect(clientsock, (struct sockaddr *) &client,
                         sizeof(client))) < 0)               
                die("Connection to MIT server failed!");                        
                                                                                
        cprintf("Sending request to MIT...\n");                                 
        r = snprintf(buffer, BUFFSIZE, "GET /usmanm/Public/ HTTP/1.1\r\n" 
                     "Host: web.mit.edu\r\n" "\r\n");                           
                                                                                
        if ((r = write(clientsock, buffer, r)) < 0)                             
                die("Request to MIT failed...\n");                              
                                                                                
        while (1)                                                               
        {                                                                       
                cprintf("Waiting for response from MIT...\n");                  
                // Receive message                                              
                if ((r = read(clientsock, buffer, BUFFSIZE)) < 0)               
                        panic("failed to read");                                
                                                                                
                cprintf("Received: %s\n", buffer);                              
                                                                                
                // no keep alive                                                
                break;                                                          
        }                                                                       
        close(clientsock);
}  
