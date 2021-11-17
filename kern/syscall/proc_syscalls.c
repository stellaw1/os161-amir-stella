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

// free copied argument buffer
void
kfree_buf(char **buf) {
    for (int i = 0; buf[i] != NULL; i++) {
        kfree(buf[i]);
    }
    kfree(buf);
}

void
kfree_newas(struct addrspace *oldas, struct addrspace *newas) {
    proc_setas(oldas);
    as_activate();
    as_destroy(newas);
}

int
get_arglen(int arglen) {
    if (arglen % 4 == 0) {
        return arglen;
    }
    return (arglen / 4 + 1) * 4;
}

/*
 * execute a program
 * ------------
 *
 * program:     path name of program to replace current program with
 * args:        array of 0 terminated strings; array terminated by NULL pointer
 * 
 * returns:     returns the process id of the new child process in the parent process; 
 *              returns 0 in the child process
 */
int execv(const char *program, char **args)
{
    int argc;
    int err = 0;
    char **arg_pointer = kmalloc(sizeof(char*));
    for (argc = 0; argc <= ARG_MAX && args[argc] != NULL; argc++);

    if (argc > ARG_MAX) {
        return E2BIG;
    }

    // create buffer for copying argument strings
    char **argsbuf = kmalloc(sizeof(char*) * argc);
    if (argsbuf == NULL) {
        kfree(arg_pointer);
        return ENOMEM;
    }

    int bufsize = 0;

    for (int i = 0; i < argc; i++) {
        // copy in pointer to argument string for security
        err = copyin((userptr_t) &args[i], arg_pointer, sizeof(char*));
        if (err) {
            kfree(arg_pointer);
            kfree(argsbuf);
            return err;
        }
        int arglen = strlen(args[i]) + 1;
        argsbuf[i] = kmalloc(sizeof(char) * arglen);
        if (argsbuf[i] == NULL) {
            kfree(arg_pointer);
            kfree_buf(argsbuf);
            return ENOMEM;
        } 
        err = copyinstr((userptr_t) args[i], argsbuf[i], sizeof(char) * arglen, NULL);
        if (err) {
            kfree(arg_pointer);
            kfree_buf(argsbuf);
            return err;
        }
        bufsize += get_arglen(arglen);
    }

    kfree(arg_pointer);

    char *progname = kmalloc(strlen(program) + 1);
    if (progname == NULL) {
        kfree_buf(argsbuf);
        return ENOMEM;
    }
    err = copyinstr((userptr_t) program, progname, strlen(program) + 1, NULL);
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        return err;
    }

    struct vnode *prog_vn;
    err = vfs_open(progname, O_RDONLY, 0, &prog_vn);
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        return err;
    }
    kfree(progname);

    struct addrspace *newas = as_create();
    if (newas == NULL) {
        kfree_buf(argsbuf);
        return ENOMEM;
    }

    struct addrspace *oldas = proc_setas(newas);
    as_activate();

    vaddr_t entrypoint;

    err = load_elf(prog_vn, &entrypoint);
    if (err) {
        kfree_buf(argsbuf);
        kfree_newas(oldas, newas);
        return err;
    }

    vaddr_t stackptr;
    err = as_define_stack(newas, &stackptr);
    if (err) {
        kfree_buf(argsbuf);
        kfree_newas(oldas, newas);
        return err;
    }
    
    // create array for storing argument locations in user stack
    char **arg_locs = kmalloc(sizeof(char *) * (argc + 1));
    if (arg_locs == NULL) {
        kfree_buf(argsbuf);
        kfree_newas(oldas, newas);
        return ENOMEM;
    }

    stackptr -= bufsize;
    for (int i = 0; i < argc; i++) {
        int arglen = strlen(argsbuf[i]) + 1;
        err = copyoutstr(argsbuf[i], (userptr_t) stackptr, arglen, NULL);
        if (err) {
            kfree_buf(argsbuf);
            kfree_newas(oldas, newas);
            kfree(arg_locs);
            return err;
        }
        arg_locs[i] = (char *) stackptr;
        stackptr += get_arglen(arglen);
    }

    arg_locs[argc] = 0;

    kfree_buf(argsbuf);

    stackptr -= (bufsize + (argc + 1) * (sizeof(char *)));

    for (int i = 0; i < argc + 1; i++) {
        err = copyout(arg_locs[i], (userptr_t) stackptr, sizeof(char *));
        if (err) {
            kfree_newas(oldas, newas);
            kfree(arg_locs);
            return err;
        }
        stackptr += sizeof(char *);
    }

    kfree(arg_locs);
    stackptr -= (argc + 1) * (sizeof(char *));

    as_destroy(oldas);

    enter_new_process(argc, (userptr_t) stackptr, NULL, stackptr, entrypoint);
    return 0;
}

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
    /*
     * error checking
     */ 
    if (options != 0) {
        return EINVAL;
    }

    if (!get_pid_in_table(pid)){
        return ESRCH;
    }

    // get curproc pid
    pid_t curpid = curproc->pid;

    // check that we are parent of child proc pointed to by pid
    if (get_parent_pid(pid) != curpid) {
        return ECHILD;
    }


    int result, exitStatus;

    // scenario 2: child proc w pid has already exited
    // just return exitCode
    if (get_pid_has_exited(pid)) {
        // set exit status and return val
        if (status != NULL) { //TODO
            exitStatus = get_pid_exitStatus(pid);

            result = copyout(&exitStatus, status, sizeof(int));
            if (result) {
                // EFAULT
                return result;
            }
        }
    } 
    else { // scenario 1: parent is blocked until child exits
        
        // decrement child_lock semaphore count
        struct semaphore *exitLock = get_exitLock(pid);
        // blocks until exit syscall is called on proc with pid entry holding lock
        P(exitLock);

        // set exit status value only if status pointer is proper TODO
        if (status != NULL) {

            exitStatus = get_pid_exitStatus(pid);

            // set exit status of pid process in location pointed to by status
            result = copyout(&exitStatus, status, sizeof(int));
            if (result) {
                // EFAULT
                return result;
            }
        }

        // get pid process and destroy
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
    // deal with children
    lock_acquire(curproc->childProcsLock);
    while (curproc->childProcs->num > 0) {
        struct proc *childProc = array_get(curproc->childProcs, 0);
        if (get_pid_has_exited(childProc->pid)) {
            proc_destroy(childProc);
        } else {
            childProc->parentDead = true;
        }

        array_remove(curproc->childProcs, 0);
    }

    lock_release(curproc->childProcsLock);



    pid_t curpid = curproc->pid;

    // set pid and proc exit status
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
