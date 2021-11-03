#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

#include <cdefs.h>
#include <kern/seek.h>


/*
 * process syscall functions
 */
int fork(struct trapframe tf, int *retval);
// int execv(const char *program, char **args);
// int waitpid(int pid, userptr_t status, int options, int *retval);
// int _exit(int exitcode);
// int getpid(int *retval);

#endif
