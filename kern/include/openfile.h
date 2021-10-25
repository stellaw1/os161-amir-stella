#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>

/*
 * Structure representing an open file entry in the open file table
 */
struct open_file
{
	struct vnode *vn;
	int flag;
	
	off_t offset;
	int refcount;
	struct lock *flock;
};

/*
 * functions
 */
struct open_file *open_file_create(struct vnode *vn, int flag);
void open_file_destroy(struct open_file *of);

void open_file_incref(struct open_file *of);
void open_file_decref(struct open_file *of);

#endif /* _OPENFILE_H_ */
