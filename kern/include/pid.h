#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>

/*
 * Structure representing an open file entry in the open file table
 */
struct pid
{
	pid_t parentPid;

	bool status;

  	// struct semaphore *p_sem;
};

/*
 * functions
 */


#endif /* _PID_H_ */
