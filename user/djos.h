#ifndef JOS_USER_DJOS_H
#define JOS_USER_DJOS_H

#define debug 1

#define SERVIP 0x12bb0047 // Server ip
#define SERVPORT 26591    // Server port  

/* Common params */
#define BUFFSIZE 1518   // Max packet size
#define MAXPENDING 5    // Max connection requests

/* Server params */
#define SLEASES 5 // server # of leases 
#define SPORT 7
#define GCTIME 300*1000   // Seconds after which abort

/* Client params */
#define RETRIES 5       // # of retries
#define CLEASES 5       // # of client leases
#define CLIENTIP 0x7f000001 // 127.0.0.1
#define CLIENTPORT 8
#define IPCRCV (UTEMP + PGSIZE) // page to map ipc rcv

/* Protocol message types */
#define PAGE_REQ 0
#define START_LEASE 1
#define DONE_LEASE 2
#define ABORT_LEASE 3
#define EXECUTED_LEASE 4

/* Error codes (reuse E_NO_MEM) */
#define E_BAD_REQ 200
#define E_NO_LEASE 201
#define E_FAIL 202

#endif // JOS_USER_DJOS_H