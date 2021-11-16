#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <synch.h>
#include <limits.h>

/*
 * Structure representing a pid entry in the pid table
 */
struct pid
{
	pid_t parentPid;
	struct semaphore *child_lock;

	bool exitFlag;
	int exitStatus;
};


#endif /* _PID_H_ */
