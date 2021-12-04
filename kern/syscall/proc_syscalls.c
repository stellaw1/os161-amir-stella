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
 * returns:     returns the process id of the new child process in the parent
 *              process
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

/*
 * helper function for freeing the kernel argument buffer
 */
void
kfree_buf(char **buf, int len)
{
    for (int i = 0; i < len; i++) {
        kfree(buf[i]);
    }
    kfree(buf);
}

/*
 * helper function for freeing the newly creating address space
 * and setting the current address space to the old one
 */
void
kfree_newas(struct addrspace *oldas, struct addrspace *newas)
{
    proc_setas(oldas);
    as_activate();
    as_destroy(newas);
}

/*
 * helper function to get the properly aligned argument size
 */
int
get_arglen(int arglen)
{
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
 * returns:     returns the process id of the new child process in the parent 
 *              process;
 *              returns 0 in the child process
 */
int execv(const char *program, char **args)
{
    int argc;
    int err = 0;
    char **arg_pointer = kmalloc(sizeof(char*));

    // copyin the args array to ensure it is a valid pointer
    err = copyin((userptr_t) args, arg_pointer, sizeof(char**));
    if (err) {
        kfree(arg_pointer);
        return err;
    }

    // loop through the args array to get the number of arguments (argc)
    for (argc = 0; argc <= ARG_MAX && args[argc] != NULL; argc++);

    if (argc > ARG_MAX) {
        kfree(arg_pointer);
        return E2BIG;
    }

    // create buffer for copying argument strings
    char **argsbuf = kmalloc(sizeof(char*) * argc);
    if (argsbuf == NULL) {
        kfree(arg_pointer);
        return ENOMEM;
    }

    int bufsize = 0;

    char *arg_test = kmalloc(sizeof(char) * ARG_MAX);
    if (arg_test == NULL) {
        kfree(arg_pointer);
        kfree(argsbuf);
        return ENOMEM;
    }

    for (int i = 0; i < argc; i++) {
        // copy in pointer to argument string to ensure it is valid
        err = copyin((userptr_t) args + i * sizeof(char *), arg_pointer,
            sizeof(char*));
        if (err) {
            kfree(arg_pointer);
            kfree(argsbuf);
            return err;
        }
        
        // copy in argument string before getting string length to 
        // ensure it is valid
        err = copyinstr((userptr_t) args[i], arg_test, sizeof(char) * ARG_MAX,
            NULL);
        if (err) {
            kfree(arg_pointer);
            kfree(argsbuf);
            kfree(arg_test);
            return err;
        }
        
        int arglen = strlen(args[i]) + 1;
        argsbuf[i] = kmalloc(sizeof(char) * arglen);
        if (argsbuf[i] == NULL) {
            kfree(arg_pointer);
            kfree_buf(argsbuf, i-1);
            kfree(arg_test);
            return ENOMEM;
        }
        
        // copy argument string into kernel buffer
        err = copyinstr((userptr_t) args[i], argsbuf[i], sizeof(char) * arglen,
            NULL);
        if (err) {
            kfree(arg_pointer);
            kfree_buf(argsbuf, i);
            kfree(arg_test);
            return err;
        }
        
        // increase buffer size by aligned argument length
        bufsize += get_arglen(arglen);
    }

    kfree(arg_test);
    kfree(arg_pointer);

    // copy in program name into kernel space
    char *progname = kmalloc(PATH_MAX);
    if (progname == NULL) {
        kfree_buf(argsbuf, argc);
        return ENOMEM;
    }
    err = copyinstr((userptr_t) program, progname, PATH_MAX, NULL);
    if (err) {
        kfree_buf(argsbuf, argc);
        kfree(progname);
        return err;
    }

    // open program executable for use by load_elf
    struct vnode *prog_vn;
    err = vfs_open(progname, O_RDONLY, 0, &prog_vn);
    if (err) {
        kfree_buf(argsbuf, argc);
        kfree(progname);
        return err;
    }
    kfree(progname);

    // create new address space and set process address space to the newly
    // created address space
    struct addrspace *newas = as_create();
    if (newas == NULL) {
        kfree_buf(argsbuf, argc);
        return ENOMEM;
    }
    struct addrspace *oldas = proc_setas(newas);
    as_activate();

    vaddr_t entrypoint;

    // load executable file
    err = load_elf(prog_vn, &entrypoint);
    if (err) {
        kfree_buf(argsbuf, argc);
        kfree_newas(oldas, newas);
        return err;
    }

    vaddr_t stackptr;
    err = as_define_stack(newas, &stackptr);
    if (err) {
        kfree_buf(argsbuf, argc);
        kfree_newas(oldas, newas);
        return err;
    }

    // create array for storing argument locations in user stack
    char **arg_locs = kmalloc(sizeof(char *) * (argc + 1));
    if (arg_locs == NULL) {
        kfree_buf(argsbuf, argc);
        kfree_newas(oldas, newas);
        return ENOMEM;
    }

    // decrement stack pointer by size of argument buffer to place
    // arguments at top of new user stack
    stackptr -= bufsize;
    for (int i = 0; i < argc; i++) {
        // copy argument string onto user stack from kernel buffer
        int arglen = strlen(argsbuf[i]) + 1;
        err = copyoutstr(argsbuf[i], (userptr_t) stackptr, arglen, NULL);
        if (err) {
            kfree_buf(argsbuf, argc);
            kfree_newas(oldas, newas);
            kfree(arg_locs);
            return err;
        }
        
        // store address of current argument
        arg_locs[i] = (char *) stackptr;

        // leave extra padding so each argument is aligned to 4 bytes
        stackptr += get_arglen(arglen);
    }

    // set a null pointer at end of argument pointers array to signify end
    arg_locs[argc] = 0;

    kfree_buf(argsbuf, argc);

    // decrement stack pointer to place argument pointers on top of arguments
    // in user stack
    stackptr -= (bufsize + (argc + 1) * (sizeof(char *)));

    for (int i = 0; i < argc + 1; i++) {
        err = copyout(arg_locs + i, (userptr_t) stackptr, sizeof(char *));
        if (err) {
            kfree_newas(oldas, newas);
            kfree(arg_locs);
            return err;
        }
        stackptr += sizeof(char *);
    }

    // set the stack pointer to the new top of the stack
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
        // set exit status if status is not a NULL pointer; do nothing if it's
        // NULL
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
        // blocks until exit syscall is called on child proc with pid entry 
        // holding lock
        P(exitLock);

        // set exit status if status is not a NULL pointer; do nothing if it's
        // NULL
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
 * exitcode:    7 bit wide value that is reported to other processes via
 *              waitpid()
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
    set_pid_exitStatus(curpid, exitcode);

    // increment child_lock semaphore count to unblock parent thread waiting on
    // this thread to exit (if any)
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
