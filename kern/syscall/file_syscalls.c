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
#include <copyinout.h>

int
open(char *filename, int flags, mode_t mode) 
{
    char *kern_filename = kmalloc(sizeof(char) * PATH_MAX);
    if (kern_filename == NULL) {
        return ENOSPC;
    }
    size_t *path_len = kmalloc(sizeof(size_t));
    if (path_len == NULL) {
        kfree(kern_filename);
        return ENOSPC;
    }

    int err = copyinstr((const_userptr_t)filename, kern_filename, PATH_MAX, path_len);

    if (err) {
        kfree(path_len);
        kfree(kern_filename);
        return err;
    }

    struct vnode **ret;
    struct open_file *of;
    
    struct vnode *ptr1 = kmalloc(sizeof(struct vnode));
    if (ptr1 == NULL) {
        kfree(path_len);
        kfree(kern_filename);
        return ENOSPC;
    }
    
    ret = &ptr1;

    err = vfs_open(kern_filename, flags, mode, ret);
    
    kfree(ptr1);
        
    if (err) {
        kfree(path_len);
        kfree(kern_filename);
        return err;
    }

    of = open_file_create(*ret, 0, flags, 1);
    
    if (of == NULL) {
        kfree(path_len);
        kfree(kern_filename);
        return ENOSPC;
    }

    lock_acquire(curproc->oft->table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->oft->table[i] == NULL) {
            curproc->oft->table[i] = of;
            kfree(kern_filename);
            kfree(path_len);
            lock_release(curproc->oft->table_lock);
            return 0;
        }
    }
    lock_release(curproc->oft->table_lock);

    open_file_destroy(of);
    kfree(kern_filename);
    kfree(path_len);

    return EMFILE;
}

int
close(int fd)
{
    if (fd < 0 || fd >= OPEN_MAX || curproc->oft->table[fd] == NULL) {
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
