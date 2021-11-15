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
        return -1;
    }

    // copy stack
    child->p_addrspace = as_create();
    if (child->p_addrspace == NULL) {
        return -1;
    }
    result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result) {
        return result;
    }

    // copy open file table
    result = open_file_table_copy(curproc->oft, child->oft);
    if (result) {
        return result;
    }

    // make the new child process return to user mode with a return value of 0
    

    

    result = thread_fork("child proc", 
                        child, 
                        help_enter_forked_process, 
                        tf, 
                        0);
    if (result) {
        return -1;
    }


    // TODO return pid of child process
    // pid_t child_pid;
    // *retval = child_pid;
    (void) retval;

    return 0;
}

void
kfree_buf(char **buf) {
    for (int j = 0; j < i; j++) {
        kfree(buf[j]);
    }
    kfree(buf);
}

void
kfree_newas(struct addrspace *oldas, struct addrspace *newas) {
    proc_setas(oldas);
    as_activate();
    proc_destroy(newas);
}

int
get_arglen(char arg[ARG_MAX]) {
    int i;
    for (i = 0; arg[i] != '\0'; i++);

    // increment by one for null terminating character
    i++;
    if (i % 4 == 0) {
        return i;
    }
    return (i / 4 + 1) * 4;
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
    char *arg_pointer;
    char curarg[ARG_MAX];
    int bufsize = 0;
    for (argc = 0; argc <= ARG_MAX || args[argc] == NULL; argc++) {
        err = copyin(args[argc], &arg_pointer, sizeof(char*));
        if (err) {
            return err;
        }
        err = copyinstr(arg_pointer, curarg, sizeof(char) * ARG_MAX, NULL);
        if (err) {
            return err;
        }
        bufsize += get_arglen(curarg);
    }

    if (argc > ARG_MAX) {
        return E2BIG;
    }

    // buffer size needs to account for argument pointers (+1 for NULL pointer)
    bufsize += (argc + 1) * sizeof(char *);

    char **kbuf = kmalloc(bufsize);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    int curarg_offset = (argc + 1) * sizeof(char *);

    for (int i = 0; i < argc; i++) {
        err = copyin(args[i], &arg_pointer, sizeof(char*));
        if (err) {
            kfree(kbuf);
            return err;
        }
        err = copyinstr(arg_pointer, curarg, sizeof(char) * ARG_MAX, NULL);
        if (err) {
            kfree(kbuf);
            return err;
        }
        int str_size = get_arglen(curarg);
        memcpy(argsbuf + curarg_offset, curarg, str_size);
        curarg_offset += str_size;
        *(argsbuf + i * sizeof(char*)) = &curarg;
    }

    *(argsbuf + argc * sizeof(char*)) = 0;

    char *progname = kmalloc(strlen(program));
    if (progname == NULL) {
        kfree_buf(argsbuf);
        return ENOMEM;
    }
    err = copyin(program, progname, strlen(program));
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        return err;
    }

    struct *vnode prog_vn;
    err = vfs_open(progname, O_RDONLY, 0, &prog_vn);
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        return err;
    }

    struct addrspace *newas = as_create();
    if (newas == NULL) {
        kfree_buf(argsbuf);
        kfree(progname);
        return ENOMEM;
    }

    struct addrspace *oldas = proc_setas(newas);
    as_activate();

    vaddr_t entrypoint;

    err = load_elf(prog_vn, &entrypoint);
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        kfree_newas(oldas, newas);
        return err;
    }

    vaddr_t stackptr;
    err = as_define_stack(newas, &stackptr);
    if (err) {
        kfree_buf(argsbuf);
        kfree(progname);
        kfree_newas(oldas, newas);
        return err;
    }

    // TODO: copy arguments to new address space

    as_destroy(oldas);

    userptr_t argv, env;
    enter_new_process(argc, argv, env, stackptr, entrypoint);
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
    *retval = curproc->pid;
    return 0;
}