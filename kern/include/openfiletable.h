#ifndef _OPENFILETABLE_H_
#define _OPENFILETABLE_H_

#include <types.h>
#include <openfile.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>

struct open_file;

/*
 * Structure representing open file table 
 */
struct open_file_table
{
	struct open_file *table[OPEN_MAX];
	struct lock *table_lock;
};

/*
 * functions
 */
struct open_file_table *open_file_table_create(void);
void open_file_table_destroy(struct open_file_table *oft);

int special_fd_create(struct open_file_table *oft);


#endif /* _OPENFILETABLE_H_ */
