#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

#include <cdefs.h>
#include <kern/seek.h>
#include <limits.h>
#include <addrspace.h>

/*
 * process syscall functions
 */
int fork(struct trapframe *tf, int *retval);
int execv(const char *program, char **args);
int waitpid(int pid, userptr_t status, int options, int *retval);
int _exit(int exitcode);
int getpid(int *retval);

// helper functions
void kfree_buf(char **buf, int len);
void kfree_newas(struct addrspace *oldas, struct addrspace *newas);
int get_arglen(int arglen);

#endif
