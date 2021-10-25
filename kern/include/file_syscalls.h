#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

#include <cdefs.h>
#include <kern/seek.h>


/*
 * syscall functions
 */
int open(const_userptr_t filename, int flags, mode_t mode);
int close(int fd);
ssize_t read(int fd, userptr_t buf, size_t buflen);
ssize_t write(int fd, const_userptr_t buf, size_t nbytes);
int lseek(int fd, off_t pos, int whence, uint32_t *retval, uint32_t *retval_v1);
int chdir(const_userptr_t pathname);
int __getcwd(userptr_t buf, size_t buflen);

#endif
