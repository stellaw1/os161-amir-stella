#ifndef _OPENFILETABLE_H_
#define _OPENFILETABLE_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>

struct open_file
{
	struct vnode *vn;
	off_t offset;
	int flags;
	int refcount;
	struct lock *flock;
};

struct open_file_table
{
	struct open_file *table[OPEN_MAX];
	struct lock *table_lock;
};

struct open_file_table *open_file_table_create();

void open_file_table_destroy(struct open_file_table *oft);

struct open_file *open_file_create();

void open_file_destroy(struct open_file *of);

#endif /* _OPENFILETABLE_H_ */
