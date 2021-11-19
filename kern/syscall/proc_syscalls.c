#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <synch.h>
#include <proc_syscalls.h>
#include <proc.h>
#include <vfs.h>
#include <openfiletable.h>
#include <openfile.h>
#include <limits.h>
#include <copyinout.h>
#include <uio.h>
#include <kern/iovec.h>
#include <stat.h>
#include <kern/fcntl.h>
#include <endian.h>
#include <kern/seek.h>
#include <kern/unistd.h>
#include <addrspace.h>
#include <kern/wait.h>

/*
 * Support functions.
 */
static
void help_enter_forked_process(void *ptr, unsigned long nargs)
{
    (void) nargs;
    enter_forked_process((struct trapframe *) ptr);
}

/*
 * copy the current process
 * ------------
 *
 * tf:          trapframe of parent process
 *
 * returns:     returns the process id of the new child process in the parent process;
 *              returns 0 in the child process
 */
int
fork(struct trapframe *tf, int *retval)
{
    int result;


    // create new process w same name as current process
    struct proc *child;
    child = proc_create_runprogram("childproc");
    if (child == NULL) {
        return ENPROC;
    }

    // add child pid to array
    array_add(curproc->childProcs, child, NULL);

    // copy stack
    result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result) {
        proc_destroy(child);
        return result;
    }

    child->oft = open_file_table_create();
	if (child->oft == NULL) {
		proc_destroy(child);
		return ENOMEM;
	}

    // copy open file table
    result = open_file_table_copy(curproc->oft, child->oft);
    if (result) {
        proc_destroy(child);
        return result;
    }

    struct trapframe *tempTfCopy = kmalloc(sizeof(struct trapframe));
    if (tempTfCopy == NULL) {
        kfree(tempTfCopy);
        proc_destroy(child);
        return ENOMEM;
    }

    memcpy(tempTfCopy, tf, sizeof(struct trapframe));

    result = thread_fork("child proc",
                        child,
                        help_enter_forked_process,
                        tempTfCopy,
                        0);
    if (result) {
        proc_destroy(child);
        return -1;
    }

    // return pid of child process
    *retval = child->pid;

    return 0;
}

// /*
//  * execute a program
//  * ------------
//  *
//  * program:     path name of program to replace current program with
//  * args:        array of 0 terminated strings; array terminated by NULL pointer
//  *
//  * returns:     returns the process id of the new child process in the parent process;
//  *              returns 0 in the child process
//  */
// int execv(const char *program, char **args)
// {

// }

/*
 * wait for a process to exit
 * ------------
 *
 * pid:         specifies process to wait on
 * status:      points to encoded exit status integer
 * options:     0
 *
 * returns:     returns pid on success and -1 or error code on error
 */
int waitpid(int pid, userptr_t status, int options, int *retval)
{
    // check options is 0
    if (options != 0) {
        return EINVAL;
    }

    // check pid is valid and points to a proper pid entry
    if (!get_pid_in_table(pid)){
        return ESRCH;
    }

    // check that we are parent of child proc pointed to by pid
    pid_t curpid = curproc->pid;
    if (get_parent_pid(pid) != curpid) {
        return ECHILD;
    }


    int result, exitStatus;

    // scenario 2: child proc w pid has already exited
    if ( get_pid_exitFlag(pid) ) {
        // set exit status if status is not a NULL pointer; do nothing if it's NULL
        if (status != NULL) {
            exitStatus = get_pid_exitStatus(pid);

            result = copyout(&exitStatus, status, sizeof(int));
            if (result) {
                // EFAULT
                return result;
            }
        }
    } 
    // scenario 1: parent is blocked until child exits
    else { 
        // decrement child_lock semaphore count
        struct semaphore *exitLock = get_exitLock(pid);
        // blocks until exit syscall is called on child proc with pid entry holding lock
        P(exitLock);

        // set exit status if status is not a NULL pointer; do nothing if it's NULL
        if (status != NULL) {
            exitStatus = get_pid_exitStatus(pid);

            result = copyout(&exitStatus, status, sizeof(int));
            if (result) {
                // EFAULT
                return result;
            }
        }

        // get pid process and destroy it
        lock_acquire(curproc->childProcsLock);
        for (unsigned i = 0; i < curproc->childProcs->num; i++) {
            struct proc *childProc = array_get(curproc->childProcs, i);
            if (childProc->pid == pid) {
                array_remove(curproc->childProcs, i);
                proc_destroy(childProc);
                break;
            }
        }
        lock_release(curproc->childProcsLock);
    }

    *retval = pid;

    return 0;
}

/*
 * terminate current process
 * ------------
 *
 * exitcode:    7 bit wide value that is reported to other processes bia waitpid()
 */
int _exit(int exitcode)
{
    // set parentDead flag to true for all of my children
    lock_acquire(curproc->childProcsLock);
    while (curproc->childProcs->num > 0) {
        struct proc *childProc = array_get(curproc->childProcs, 0);
        if (get_pid_exitFlag(childProc->pid)) {
            proc_destroy(childProc);
        } else {
            childProc->parentDead = true;
        }

        array_remove(curproc->childProcs, 0);
    }

    lock_release(curproc->childProcsLock);


    pid_t curpid = curproc->pid;

    // set pid exitFlag and proc exitStatus
    set_pid_exitFlag(curpid, true);
    set_pid_exitStatus(curpid, _MKWAIT_EXIT(exitcode));

    //increment child_lock semaphore count to unblock parent thread waiting on this thread to exit (if any)
    struct semaphore *exitLock = get_exitLock(curpid);
    V(exitLock);

    thread_exit();

    panic("exit syscall should not reach here");
    return 0;
}

/*
 * get process id
 * ------------
 *
 * returns:     the process id of the current process
 */
int getpid(int *retval)
{
    KASSERT(curproc->pid != 0);
    KASSERT(curproc->pid != 1);

    *retval = curproc->pid;

    return 0;
}