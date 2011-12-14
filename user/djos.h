#ifndef JOS_USER_DJOS_H
#define JOS_USER_DJOS_H

#include "ip.h"

#define debug 0

/* Common params */
#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests

/* Server params */
#define SLEASES 5 // server # of leases 
#define SPORT 7
#define GCTIME 300*1000   // Seconds after which abort

/* Client params */
#define RETRIES 2       // # of retries
#define CLEASES 5       // # of client leases
#define IPCRCV (UTEMP + PGSIZE) // page to map ipc rcv
#define IPCSND (UTEMP + PGSIZE) // page to map ipc rcv
#define DJOSTEMP (UTEMP + 2*PGSIZE) // temp page
#define NSERVERS 3

/* Protocol message types */
#define PAGE_REQ 0
#define START_LEASE 1
#define DONE_LEASE 2
#define ABORT_LEASE 3
#define COMPLETED_LEASE 4
#define START_IPC 5
#define DONE_IPC 6

#define CLIENT_LEASE_REQUEST 0
#define CLIENT_LEASE_COMPLETED 1
#define CLIENT_SEND_IPC 2

/* Error codes (reuse E_NO_MEM) */
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202
#define E_NO_IPC 203

struct ipc_pkt {
	envid_t pkt_dst;
	envid_t pkt_src;
	uint32_t pkt_val;
	uintptr_t pkt_va;
	unsigned pkt_perm;
	bool pkt_toalien;
};

#endif // JOS_USER_DJOS_H
