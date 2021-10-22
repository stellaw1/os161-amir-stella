#ifndef _OPENFILETABLE_H_
#define _OPENFILETABLE_H_

#include <types.h>
#include <spinlock.h>
#include <limits.h>

struct addrspace;
struct vnode;
struct spinlock;

struct open_file
{
	struct vnode *vn;
	off_t offset;
	int flags;
	int refcount;
	struct spinlock *fd_lock;
};

struct open_file_table
{
	struct open_file *table[OPEN_MAX];
	struct lock *table_lock;
};

#endif /* _OPENFILETABLE_H_ */
