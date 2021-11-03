#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <synch.h>
#include <file_syscalls.h>
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
#include <trapframe.h>
#include <dumbvm.c>

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
fork(struct trapframe tf, int *retval) 
{
    int result;

    // create new process w same name as current process
    struct proc *child;
    child = proc_create_runprogram(curproc->p_name);

    // copy stack
    child->p_addrspace = as_create();
    result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result) {
        return result;
    }

    // init child thread open file table
    child->oft = 
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

}

/*
 * terminate current process
 * ------------
 *
 * exitcode:    7 bit wide value that is reported to other processes bia waitpid()
 */
int _exit(int exitcode)
{

}

/*
 * get process id
 * ------------
 * 
 * returns:     the process id of the current process
 */
int getpid(int *retval)
{

}