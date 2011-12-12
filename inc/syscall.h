#ifndef JOS_INC_SYSCALL_H
#define JOS_INC_SYSCALL_H

/* system call numbers */
enum {
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
	SYS_page_alloc,
	SYS_page_map,
	SYS_page_unmap,
	SYS_exofork,
	SYS_env_set_status,
	SYS_env_set_trapframe,
	SYS_env_set_pgfault_upcall,
	SYS_yield,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	SYS_ipc_set_recv,
	SYS_time_msec,
	SYS_env_swap,
	SYS_net_try_send,
	SYS_net_try_receive,
	SYS_get_mac,
	SYS_env_lease,
	SYS_env_is_leased,
	SYS_copy_mem,
	SYS_get_perms,
	SYS_env_unsuspend,
	SYS_migrate,
	NSYSCALLS
};

#endif /* !JOS_INC_SYSCALL_H */
