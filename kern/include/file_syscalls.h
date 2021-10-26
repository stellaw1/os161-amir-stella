#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

#include <cdefs.h>
#include <kern/seek.h>


/*
 * syscall functions
 */
int open(const_userptr_t filename, int flags, mode_t mode, int *retval);
int close(int fd);
int read(int fd, userptr_t buf, size_t buflen, int *retval);
int write(int fd, userptr_t buf, size_t nbytes, int *retval);
int dup2(int oldfd, int newfd, int *retval);
int lseek(int fd, off_t pos, int whence, off_t *ret_pos);
int chdir(const_userptr_t pathname);
int __getcwd(userptr_t buf, size_t buflen, int *retval);

#endif
