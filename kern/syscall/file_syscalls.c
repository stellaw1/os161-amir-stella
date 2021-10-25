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
#include <uio.h>
#include <kern/iovec.h>
#include <stat.h>

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

ssize_t
read(int fd, void *buf, size_t buflen)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    
    struct open_file *of;

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        return EBADF;
    } else {
        of = curproc->oft->table[fd];
    }
    lock_release(curproc->oft->table_lock);
    
    int result;
    char kbuf[buflen];
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *myuio = kmalloc(sizeof(struct uio));
    uio_kinit(iov, myuio, kbuf, buflen, of->offset, UIO_READ);

    lock_acquire(of->flock);
    result = VOP_READ(of->vn, myuio);
    lock_release(of->flock);
    if (result) {
        return result;
    }

    result = copyout(iov->iov_kbase, (userptr_t) buf, buflen);
    if (result) {
        return result;
    }

    return 0;
}

ssize_t 
write(int fd, const void *buf, size_t nbytes) 
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    
    struct open_file *of;

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        return EBADF;
    } else {
        of = curproc->oft->table[fd];
    }
    lock_release(curproc->oft->table_lock);

    int result;
    char kbuf[nbytes];
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *myuio = kmalloc(sizeof(struct uio));
    uio_kinit(iov, myuio, kbuf, nbytes, of->offset, UIO_WRITE);

    result = copyin((const_userptr_t) buf, iov->iov_kbase, nbytes);
    if (result) {
        return result;
    }

    lock_acquire(of->flock);
    result = VOP_WRITE(of->vn, myuio);
    lock_release(of->flock);
    if (result) {
        return result;
    }

    return 0;
}

off_t
lseek(int fd, off_t pos, int whence)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    if (fd == 0 || fd == 1 || fd == 2) {
        return ESPIPE;
    }

    struct open_file *of;

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        return EBADF;
    } else {
        of = curproc->oft->table[fd];
    }
    lock_release(curproc->oft->table_lock);

    if (of->offset < pos) {
        return EINVAL;
    }

    switch(whence) {
        case SEEK_SET:
            of->offset = pos;
            break;

        case SEEK_CUR:
            of->offset = of->offset + pos;
            break;

        case SEEK_END: ;
            struct stat *statbuf = kmalloc(sizeof(struct stat));
            int result = VOP_STAT(of->vn, statbuf);
            if (result) {
                return result;
            }
            of->offset = statbuf->st_size + pos;
            kfree(statbuf);
            break;
            
        default: 
            return EINVAL;
    }

    return of->offset;
}
