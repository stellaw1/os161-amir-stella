#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

#include <cdefs.h>

int open(char *filename, int flags, mode_t mode);
int close(int fd);

#endif
