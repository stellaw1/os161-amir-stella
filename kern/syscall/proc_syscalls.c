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
    unsigned *index_ret = kmalloc(sizeof(unsigned));
    if (index_ret == NULL) {
        return -1;
    }

    array_add(curproc->childProcs, child, index_ret);

    // copy stack
    result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result) {
        return result;
    }

    // TODO lock oft
    child->oft = open_file_table_create();
	if (child->oft == NULL) {
		proc_destroy(child);
		return ENOMEM;
	}
    // copy open file table
    result = open_file_table_copy(curproc->oft, child->oft);
    if (result) {
        return result;
    }

    struct trapframe *tempTfCopy = kmalloc(sizeof(struct trapframe));
    if (tempTfCopy == NULL) {
        return ENOMEM;
    }

    memcpy(tempTfCopy, tf, sizeof(struct trapframe));

    result = thread_fork("child proc",
                        child,
                        help_enter_forked_process,
                        tempTfCopy,
                        0);
    if (result) {
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
    if (options != 0) {
        return EINVAL;
    }
    
    if (!get_pid_in_table(pid)){
        return ESRCH;
    }

    if (get_pid_has_exited(pid)) {
        // set return val ?
        
        return 0;
    }

    int result;

    // get curproc pid
    pid_t curpid = curproc->pid;

    // check that we are parent of child proc pointed to by pid
    if (get_parent_pid(pid) != curpid) {
        return ECHILD;
    }

    // decrement child_lock semaphore count (blocks until exit syscall is called on proc with pid entry holding lock)
    struct semaphore *exitLock = get_exitLock(curpid);
    P(exitLock);

    // set exit status value only if status pointer is not NULL
    if (status != NULL) {
        int exitStatus;

        exitStatus = get_pid_exitStatus(pid);

        if (WEXITSTATUS(exitStatus)) {
            exitStatus = _MKWAIT_EXIT(exitStatus);
        } else {
            exitStatus = _MKWAIT_SIG(exitStatus);
        }

        // set exit status of pid process in location pointed to by status
        result = copyout(&exitStatus, status, sizeof(int));
        if (result) {
            return result;
        }
    }

    // get pid process and destroy
    for (unsigned i = 0; i < curproc->childProcs->num; i++) {
        struct proc *childProc = array_get(curproc->childProcs, i);
        if (childProc->pid == pid) {
            proc_destroy(childProc);
        }
    }
    
    // pid_destroy
    destroy_pid_entry(pid);

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
    pid_t curpid = curproc->pid;

    // set pid and proc exit status
    set_pid_exitFlag(curpid, true);
    set_pid_exitStatus(curpid,exitcode);
    
    //increment child_lock semaphore count
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