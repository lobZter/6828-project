#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "djos.h"

#define LEASE_REQ_SZ (1 + sizeof(struct Env) + sizeof(envid_t) + sizeof(void **))
#define PAGE_REQ_SZ (1 + sizeof(envid_t) + sizeof(uintptr_t) + sizeof(int) + 1025)
#define DONE_REQ_SZ (1 + sizeof(envid_t))
#define ABORT_REQ_SZ (1 + sizeof(envid_t))
#define LEASE_COMP_SZ (1 + sizeof(envid_t)) 
#define IPC_START_SZ (1 + sizeof(struct ipc_pkt))

int gsid = 0; // server id

struct lease_entry {
	envid_t env_id;
	uint32_t lessee_sid;
};

struct lease_entry lease_map[CLEASES];

struct server {
	uint32_t ip;
	uint16_t port;
};

struct server server_map[] = { // # must match NSERVERS
	{ 0x12bb0048, 26591 },
//	{ 0x12bb0050, 25281 },
	{ 0x12bb0049, 26591 },
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
		panic("Allocating at %x in page fault handler: %e", addr, r);
}

int
get_server()
{
	do {
		gsid = (gsid + 1) % NSERVERS; // try next server
	}
	while (server_map[gsid].ip == CLIENTIP);

	return gsid;
}

int
get_my_sid()
{
	int i;
	for (i = 0; i < NSERVERS; i++) {
		if (server_map[i].ip == CLIENTIP) return i;
	}
	
	die("Couldn't find self in sever_map!");
	return -1;
}

int 
put_lease(envid_t envid, int sid) 
{
	int i;
	
	for (i = 0; i < CLEASES; i++) {
		if (!lease_map[i].env_id) {
			lease_map[i].env_id = envid;
			lease_map[i].lessee_sid = sid;
			return i;
		}
	}

	return -E_FAIL;
}

int 
delete_lease(envid_t envid)
{
	int i;
	
	for (i = 0; i < CLEASES; i++) {
		if (lease_map[i].env_id == envid) {
			lease_map[i].env_id = 0;
			lease_map[i].lessee_sid = 0;
			return i;
		}
	}

	return -1;
}

int
find_lease(envid_t envid) 
{
	int i;
	for (i = 0; i < CLEASES; i++) {
		if (lease_map[i].env_id == envid) {
			return i;
		}
	}
	return -1;
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

	for (i = 0; i < CLEASES; i++) {
		if (!lease_map[i].env_id) continue;

		e = (struct Env *) &envs[ENVX(lease_map[i].env_id)];
		if (e->env_status != ENV_LEASED) {
			lease_map[i].env_id = 0;
			lease_map[i].lessee_sid = 0;
		}
	}
}

int
connect_serv(int sid)
{
        int r;
        int clientsock;
	struct sockaddr_in client;

        if ((clientsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                die("Doomed!");

        memset(&client, 0, sizeof(client));             // Clear struct
        client.sin_family = AF_INET;                    // Internet/IP
        client.sin_addr.s_addr = htonl(server_map[sid].ip); // client ip
        client.sin_port = htons(server_map[sid].port);  // client port

	if (debug) {
		cprintf("Connecting to server at %x:%d...\n", 
			server_map[sid].ip, server_map[sid].port);
	}

        if ((r = connect(clientsock, (struct sockaddr *) &client,
                         sizeof(client))) < 0) {              
                cprintf("Connection to server failed!\n");
		close(clientsock);
		return -E_FAIL;
	}

        if (debug) {
                cprintf("Connected to server at %x:%d.\n", 
			server_map[sid].ip, server_map[sid].port);
        }

	return clientsock;
}

int
issue_request(int sock, const void *req, int len)
{
	// For now only send status code back
	if (debug) {
		cprintf("Sending request: %d, %x\n", 
			((char *)req)[0], *((envid_t *) (req + 1)));
	}

	if (write(sock, req, len) != len) {
		if (debug)
			cprintf("Failed to send request to server!\n");
		return -1;
	}

	return len;
}

int
send_buff(const void *req, int len, int sid)
{
	int cretry = 0;
	int response;

	int sock = connect_serv(sid);
	if (sock < 0) return -E_FAIL;

	char buffer[BUFFSIZE];
<<<<<<< HEAD
	while (cretry < RETRIES) {
		if (issue_request(sock, req, len) >= 0) {
			break;
		}
		close(sock);
		sock = connect_serv(SERVIP, SERVPORT);
		cretry++;
	}
=======

	if (debug) {
		cprintf("Client sending request type: %d\n", *((char *) req));
	}

	issue_request(sock, req, len);
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116

	if (debug) {
		cprintf("Waiting for response from server...\n");   
	}

	// Receive message
	if (read(sock, buffer, BUFFSIZE) < 0) {
		panic("Failed to read");
	}

	close(sock);
	       
	if (debug) {
		cprintf("Received: %d\n", *((int *) buffer));
	}

	return *((int *) buffer);
}

int
send_lease_req(envid_t envid, void *thisenv, struct Env *env, int sid)
{
	char buffer[LEASE_REQ_SZ];
	int r;
	struct Env *e;

	// Clear buffer
	memset(buffer, 0, LEASE_REQ_SZ);
	
	*((char *) buffer) = START_LEASE;
	*((envid_t *)(buffer + 1)) = envid;
	e = (struct Env *) (buffer + sizeof(envid_t) + 1);

	memmove(e, (void *) env, sizeof(struct Env));
	*((void **)(buffer + 1 + sizeof(struct Env) + sizeof(envid_t)))
		= thisenv;

	if (debug){
		cprintf("Sending struct Env: \n"
			"  env_id: %x\n"
			"  env_parent_id: %x\n"
			"  env_status: %x\n"
			"  env_hostsid: %x\n",
			e->env_id, e->env_parent_id,
			e->env_status, e->env_hostsid);
	}
	
	return send_buff(buffer, LEASE_REQ_SZ, sid);
}

int
send_page_req(envid_t envid, uintptr_t va, int perm, int sid)
{
	int r, i, offset;
	char buffer[PAGE_REQ_SZ];
	char *s;

	offset = 0;

	* (char *) buffer = PAGE_REQ;
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

		r = send_buff(buffer, PAGE_REQ_SZ, sid);
		if (r < 0) return r;
	}

	return 0;
}

int
send_pages(envid_t envid, int sid)
{
	uintptr_t addr;
	int r, perm;

	for (addr = UTEXT; addr < UTOP; addr += PGSIZE){
		if (sys_get_perms(envid, (void *) addr, &perm) < 0) {
			continue;
		};

		r = send_page_req(envid, addr, perm, sid);
		if (r < 0) return r;
	}

	return 0;
}

int
send_done_request(envid_t envid, uint8_t code, int sid)
{
	char buffer[DONE_REQ_SZ];
	
	switch (code)
	{
	case DONE_LEASE:
		goto correct_code;
	case DONE_IPC:
		goto correct_code;
	default:
		return -E_INVAL;
	}
correct_code:
	buffer[0] = code;
	*((envid_t *) (buffer + 1)) = envid;
	return send_buff(buffer, DONE_REQ_SZ, sid);
}

int
send_abort_request(envid_t envid, int sid) 
{
	char buffer[ABORT_REQ_SZ];

	buffer[0] = ABORT_LEASE;
	*((envid_t *) (buffer + 1)) = envid;
	return send_buff(buffer, ABORT_REQ_SZ, sid);
}

int
send_env(struct Env *env, void *thisenv, int sid)
{
	int r, cretry = 0;
	uintptr_t addr;
	
	while (cretry < RETRIES) {
		cretry++;

		r = send_lease_req(env->env_id, thisenv, env, sid);
		if (r == -E_FAIL || r == -E_NO_LEASE) continue;
		if (r < 0) goto error;
		
		if (debug) {
			cprintf("Leased request sent for %x.\n", env->env_id);
		}

		r = send_pages(env->env_id, sid);
		if (r == -E_FAIL) continue;
		if (r == -E_NO_MEM) goto error;

		if (r < 0) goto error;

		r = send_done_request(env->env_id, DONE_LEASE, sid);
		if (r == -E_BAD_REQ) continue;
		if (r < 0) goto error;

		break;
	error:
		send_abort_request(env->env_id, sid);
	}

	if (cretry >= RETRIES) {
		return -E_FAIL;
	}

	return 0;
}

void
try_send_lease(envid_t envid, void *thisenv)
{
	struct Env e;
	int r, sid;

	// Get envid from ipc *value*
	memmove((void *) &e, (void *) &envs[ENVX(envid)], 
		sizeof(struct Env));

	// Ids must match
	if (e.env_id != envid) {
<<<<<<< HEAD
		cprintf("Env id mismatch!\n");
=======
		cprintf("Env id mismatch in lease! %x, %x\n", e.env_id, envid);
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116
		return;
	}

	// Status must be ENV_LEASED
	if (e.env_status != ENV_SUSPENDED) {
		cprintf("Failed to lease envid %x. Not suspended!\n", 
			envid);
		r = -E_FAIL;
	}
	else {
		// Set eax to 0, to appear migrate call succeed
		e.env_tf.tf_regs.reg_eax = 0;
			
		// Find server to send
		sid = get_server();

		// Put in lease_map
		if ((r = put_lease(envid, sid)) >= 0) {
			// Set own ip ports
			e.env_hostsid = get_my_sid();

			// Try sending env
			r = send_env(&e, thisenv, sid);
		}
	}

	// If lease failed, then set eax to -1 to indicate failure
	// And mark ENV_RUNNABLE
	if (r < 0) {
		cprintf("Lease to server failed! Aborting...\n");
		sys_env_unsuspend(envid, ENV_RUNNABLE, r);
		delete_lease(envid);
	}
	else {
		// Do what?
		sys_env_unsuspend(envid, ENV_LEASED, 0);
	}

}

void
try_send_lease_completed(envid_t envid)
{
	struct Env *e;
	int r, i, ctries = 0;
	char buffer[LEASE_COMP_SZ];
	
	e = (struct Env *) &envs[ENVX(envid)];

	// Ids must match
	if (e->env_id != envid) {
		cprintf("Env id mismatch in completed %x, %x!\n",
			e->env_id, envid);
		return;
	}

	// Status must be ENV_SUSPENDED
	if (e->env_status != ENV_SUSPENDED) {
		cprintf("Failed to lease complete envid %x. Not suspended!\n", 
			envid);
		r = -E_FAIL;
		goto end;
	}

	cprintf("Finished executing process %08x->%08x.\n", 
		e->env_id, e->env_hosteid);

	while (ctries < RETRIES) {
		ctries++;

		buffer[0] = COMPLETED_LEASE;
		*((envid_t *) (buffer + 1)) = e->env_hosteid;
		r = send_buff(buffer, LEASE_COMP_SZ, e->env_hostsid);

		if (!r || r == -E_BAD_REQ) break;
	}

	if (ctries >= RETRIES) {
		r = -E_FAIL;
		goto end;
	}

end:
	if (r < 0) {
		cprintf("Complete lease to server failed! Aborting...\n");
		sys_env_unsuspend(envid, ENV_RUNNABLE, r);
	}
	else {
		// Do what?
		sys_env_unsuspend(envid, ENV_RUNNABLE, 0);
	}
}

int
send_ipc_start(struct ipc_pkt *packet, int sid)
{
	char buffer[IPC_START_SZ];
	int r;

	// Clear buffer
	memset(buffer, 0, sizeof(struct ipc_pkt) + 1);
	
	*((char *) buffer) = START_IPC;
	memmove((void *) (buffer + 1), 
		(void *) packet, sizeof(struct ipc_pkt));

	if (debug){
		cprintf("Sending IPC Start: \n"
			"  src_id: %x\n"
			"  dst_id: %x\n"
<<<<<<< HEAD
			"  val: %d\n",
			"  fromalien: %d\n",
			packet->pkt_src, packet->pkt_dst, packet->pkt_val,
			packet->pkt_fromalien);
=======
			"  val: %d\n"
			"  toalien?: %d\n",
			packet->pkt_src, packet->pkt_dst, packet->pkt_val,
			packet->pkt_toalien);
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116
	}
	
	return send_buff(buffer, IPC_START_SZ, sid);
}

int
send_ipc_req(struct ipc_pkt *packet, int sid)
{
	int r, cretry = 0;
	
	while (cretry <= RETRIES) {
		cretry++;

		r = send_ipc_start(packet, sid);

		switch (r) {
		case -E_NO_IPC:
			return -E_IPC_NOT_RECV;
		case -E_BAD_REQ:
			return -E_INVAL;
		case -E_FAIL:
			return -E_BAD_ENV;
		case 0:
			return 0;
		default:
			continue;
		}
	}

	if (cretry > (RETRIES + 1)) return -E_INVAL;

	return 0;
}
void
try_send_ipc(uintptr_t va)
{
<<<<<<< HEAD
	struct Env e, s;
	uint32_t ip = 0;
	int r;
	struct ipc_pkt packet;
	
	packet.pkt_src = src_id;
	packet.pkt_dst = *((envid_t *) va);
	packet.pkt_val = *((uint32_t *) (va + sizeof(envid_t)));
	packet.pkt_va = 0;
	packet.pkt_perm = *((unsigned *) (va + sizeof(envid_t) + 
					  sizeof(uint32_t)));
	packet.pkt_fromalien = 0;

	// Get envid from ipc *value*, check env exists
	memmove((void *) &e, (void *) &envs[ENVX(packet.pkt_dst)], 
		sizeof(struct Env));
	memmove((void *) &s, (void *) &envs[ENVX(src_id)], 
		sizeof(struct Env));

	if (s.env_alien) {
		packet.pkt_src = s.env_hosteid;
		packet.pkt_fromalien = 1;
		ip = s.env_hostip;
	}
	else {
                // Ids must match
		if (e.env_id != packet.pkt_dst) {
			cprintf("Env id mismatch!\n");
			r = -E_BAD_ENV;
			goto ipc_end;
		}

		// Status must be ENV_LEASED
		if (e.env_status != ENV_LEASED) {
			cprintf("Sending IPC via DJOS to unleased"
				" process or from non-alien!\n", 
				e.env_id);
			r = -E_BAD_ENV;
			goto ipc_end;
		}
		
                // Put in lease_map
		if ((r = find_lease(packet.pkt_dst) >= 0)) {
			ip = lease_map[r].lessee_ip;
=======
	struct Env *d, *s;
	int sid;
	int r;
	struct ipc_pkt packet;
	bool snd_alien;

	packet.pkt_src = *((envid_t *) va);
	packet.pkt_dst = *((envid_t *) (va + sizeof(envid_t)));
	packet.pkt_val = *((uint32_t *) (va + 2*sizeof(envid_t)));
	packet.pkt_va = *((uintptr_t *) (va + 2*sizeof(envid_t) + 
				     sizeof(uint32_t)));
	packet.pkt_perm = *((unsigned *) (va + 2*sizeof(envid_t) + 
					  sizeof(uint32_t) + 
					  sizeof (void *)));
	packet.pkt_toalien = *((bool *) (va + 2*sizeof(envid_t) +
					 sizeof(uint32_t) +
					 sizeof (void *) + 
					 sizeof(unsigned)));

	// Don't allow sending pages right now!
	packet.pkt_va = UTOP;

	// Get envid from ipc *value*, check env exists
	s = (struct Env *) &envs[ENVX(packet.pkt_src)];
	d = (struct Env *) &envs[ENVX(packet.pkt_dst)];

	// Sending from native machine to leased env
	if (packet.pkt_toalien) {
		// Ids must match
		if (d->env_id != packet.pkt_dst) {
			cprintf("Env id mismatch in ipc %x, %x!\n",
				d->env_id, packet.pkt_dst);
			r = -E_BAD_ENV;
			goto ipc_done;
		}

		// Status must be ENV_LEASED
		if (d->env_status != ENV_LEASED) {
			cprintf("Sending IPC via DJOS to unleased process!\n", 
				d->env_id);
			r = -E_BAD_ENV;
			goto ipc_done;
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116
		}

		// Check in lease_map
		if ((r = find_lease(packet.pkt_dst) < 0)) {
			cprintf("Sending IPC to DJOS to env not in map\n");
			r = -E_BAD_ENV;
<<<<<<< HEAD
			goto ipc_end;
=======
			goto ipc_done;
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116
		}

		sid = lease_map[r].lessee_sid;
	}
	// Sending from alien machine to host machine
	else {
		packet.pkt_src = s->env_hosteid;
		sid = s->env_hostsid;
	}

	r = send_ipc_req(&packet, sid);

<<<<<<< HEAD
	// Try sending env
	r = send_ipc_req(&packet, ip);

	ipc_end:
=======
ipc_done:
>>>>>>> 89f6a7ac0a75f3a5bcb326ec156704b5edf25116
	// If ipc failed, then set eax to r to indicate failure
	// And mark ENV_RUNNABLE
	if (r < 0) {
		if (r != -E_IPC_NOT_RECV) {
			cprintf("IPC to server failed! Aborting...\n");
		}
		sys_env_unsuspend(s->env_id, ENV_RUNNABLE, r);
	}
	else {
		// Mark success and run again
		sys_env_unsuspend(s->env_id, ENV_RUNNABLE, 0);
	}
}

void
process_request()
{
	char icode = dipc_recv();

	switch(icode)
	{
	case CLIENT_LEASE_REQUEST:
		try_send_lease(*((envid_t *) IPCRCV), 
			       *((void **) (IPCRCV + sizeof(envid_t))));
		return;
	case CLIENT_LEASE_COMPLETED:
		try_send_lease_completed(*((envid_t *) IPCRCV));
		return;
	case CLIENT_SEND_IPC:
		try_send_ipc((uintptr_t) IPCRCV);
		return;
	default:
		return;
	}
}

void
umain(int argc, char **argv)
{
	// Set page fault handler
	set_pgfault_handler(pg_handler);

	while (1) {
		// GC completed leases
		check_lease_complete();

		if (debug) {
			cprintf("Waiting for requests on client %x...\n",
				thisenv->env_id);
		}

		process_request();
	}
}
