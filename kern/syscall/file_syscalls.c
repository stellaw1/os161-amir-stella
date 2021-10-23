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
#include <limits.h>

int
open(char *filename, int flags, mode_t mode) 
{
    struct vnode **ret;
    struct open_file *of;
    
    struct vnode *ptr1 = kmalloc(sizeof(struct vnode));
    if (ptr1 == NULL) {
        return ENOSPC;
    }
    
    ret = &ptr1;

    int err = vfs_open(filename, flags, mode, ret);
    
    kfree(ptr1);
        
    if (err) {
        return err;
    }

    of = open_file_create(*ret, 0, flags, 1);
    
    if (of == NULL) {
        return ENOSPC;
    }

    lock_acquire(curproc->oft->table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->oft->table[i] == NULL) {
            curproc->oft->table[i] = of;
            lock_release(curproc->oft->table_lock);
            return 0;
        }
    }
    lock_release(curproc->oft->table_lock);

    open_file_destroy(of);

    return EMFILE;
}

int
close(int fd)
{
    if (fd < 0 || fd > OPEN_MAX || curproc->oft->table[fd] == NULL) {
        return EBADF;
    }

    vfs_close(curproc->oft->table[fd]->vn);
    curproc->oft->table[fd]->refcount--;

    if (curproc->oft->table[fd]->refcount == 0) {
        open_file_destroy(curproc->oft->table[fd]);
    }

    curproc->oft->table[fd] = NULL;
    return 0;
}
