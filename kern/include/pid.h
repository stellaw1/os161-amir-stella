#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <synch.h>
#include <limits.h>

/*
 * Structure representing a pid entry in the pid table; associated with one process
 */
struct pid
{

	pid_t parentPid;				/* pid of the parent of associated process */
	struct semaphore *exitLock;		/* lock used to signal to blocking parent that associated process has exited */

	bool exitFlag;					/* associated process has exited */
	int exitStatus;					/* exitCode called on associated process to exit */
};


#endif /* _PID_H_ */
