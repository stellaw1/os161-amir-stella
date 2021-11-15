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

    // TODO add child pid to array

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
    int bufsize = 0;
    for (argc = 0; argc <= ARG_MAX && args[argc] != NULL; argc++) {
        bufsize += strlen(args[argc]) + 1;
    }

    if (argc > ARG_MAX) {
        return E2BIG;
    }

    // buffer size needs to account for argument pointers (+1 for NULL pointer)
    bufsize += (argc + 1) * sizeof(char *);

    char **argsbuf = kmalloc(bufsize);
    if (argsbuf == NULL) {
        return ENOMEM;
    }

    int curarg_offset = (argc + 1) * sizeof(char *);

    for (int i = 0; i < argc; i++) {
        err = copyin((userptr_t) &args[i], arg_pointer, sizeof(char*));
        if (err) {
            kfree(argsbuf);
            return err;
        }
        int arglen = strlen(args[i]) + 1;
        err = copyinstr((userptr_t) args[i], (char *) (argsbuf + curarg_offset), sizeof(char) * arglen, NULL);
        if (err) {
            kfree(argsbuf);
            return err;
        }
        *(argsbuf + i * sizeof(char*)) = (char*) (argsbuf + curarg_offset);
        arglen = get_arglen(arglen);
        curarg_offset += arglen;
    }

    *(argsbuf + argc * sizeof(char*)) = 0;

    char *progname = kmalloc(strlen(program) + 1);
    if (progname == NULL) {
        kfree(argsbuf);
        return ENOMEM;
    }
    err = copyinstr((userptr_t) program, progname, strlen(program) + 1, NULL);
    if (err) {
        kfree(argsbuf);
        kfree(progname);
        return err;
    }

    struct vnode *prog_vn;
    err = vfs_open(progname, O_RDONLY, 0, &prog_vn);
    if (err) {
        kfree(argsbuf);
        kfree(progname);
        return err;
    }

    struct addrspace *newas = as_create();
    if (newas == NULL) {
        kfree(argsbuf);
        kfree(progname);
        return ENOMEM;
    }

    struct addrspace *oldas = proc_setas(newas);
    as_activate();

    vaddr_t entrypoint;

    err = load_elf(prog_vn, &entrypoint);
    if (err) {
        kfree(argsbuf);
        kfree(progname);
        proc_setas(oldas);
        as_activate();
        as_destroy(newas);
        return err;
    }

    vaddr_t stackptr;
    err = as_define_stack(newas, &stackptr);
    if (err) {
        kfree(argsbuf);
        kfree(progname);
        proc_setas(oldas);
        as_activate();
        as_destroy(newas);
        return err;
    }
    
    int args_stack_start = stackptr - bufsize;
    curarg_offset = args_stack_start + (argc + 1) * sizeof(char*);
    for (int i = 0; i < argc; i++) {
        int arglen = strlen(*(argsbuf + i * sizeof(char*)));
        arglen = get_arglen(arglen);
        *(argsbuf + i * sizeof(char*)) = (char*) (curarg_offset);
        curarg_offset += arglen;
    }
    
    err = copyout(argsbuf, (userptr_t) args_stack_start, bufsize);
    if (err) {
        kfree(argsbuf);
        kfree(progname);
        proc_setas(oldas);
        as_activate();
        as_destroy(newas);
        return err;
    }

    stackptr -= bufsize;

    as_destroy(oldas);

    enter_new_process(argc, (userptr_t) stackptr, NULL, stackptr, entrypoint);
    return 0;
}

// /*
//  * wait for a process to exit
//  * ------------
//  *
//  * pid:         specifies process to wait on
//  * status:      points to encoded exit status integer
//  * options:     0
//  *
//  * returns:     returns pid on success and -1 or error code on error
//  */
// int waitpid(int pid, userptr_t status, int options, int *retval)
// {

// }

// /*
//  * terminate current process
//  * ------------
//  *
//  * exitcode:    7 bit wide value that is reported to other processes bia waitpid()
//  */
// int _exit(int exitcode)
// {

// }

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
