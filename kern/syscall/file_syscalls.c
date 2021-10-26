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
#include <kern/fcntl.h>
#include <endian.h>

/*
 * open a file
 * ------------
 *
 * filename:    pathname of file/kernel obj
 * flags:       specifies how to open the file
 * mode:        provides the file permissions to use
 * 
 * returns:     a nonnegative file handle on success and -1 or errno on error
 */
int
open(const_userptr_t filename, int flags, mode_t mode, int *retval) 
{
    //check flags
    
    int result;
    char *kern_filename;
    kern_filename = kmalloc(PATH_MAX);
    if (kern_filename == NULL) {
        return ENOSPC;
    }
    size_t *path_len;
    path_len = kmalloc(sizeof(size_t));
    if (path_len == NULL) {
        kfree(kern_filename);
        return ENOSPC;
    }

    result = copyinstr(filename, kern_filename, PATH_MAX, path_len);
    if (result) {
        kfree(path_len);
        kfree(kern_filename);
        return result;
    }

    struct vnode *ret;
    struct open_file *of;
    
    result = vfs_open(kern_filename, flags, mode, &ret);
    if (result) {
        kfree(path_len);
        kfree(kern_filename);
        return result;
    }

    of = open_file_create(ret, flags);
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
            *retval = i;
            return 0;
        }
    }
    lock_release(curproc->oft->table_lock);

    open_file_destroy(of);
    kfree(kern_filename);
    kfree(path_len);

    return EMFILE;
}

/*
 * close a file
 * ------------
 *
 * fd:          file descriptor of file being closed
 * 
 * returns:     0 on success and -1 or error code on error
 */
int
close(int fd)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        lock_release(curproc->oft->table_lock);
        return EBADF;
    }
    open_file_decref(curproc->oft->table[fd]);
    curproc->oft->table[fd] = NULL;
    lock_release(curproc->oft->table_lock);

    return 0;
}

/*
 * read data from file 
 * -------------------
 *
 * fd:          file descriptor of file to read from
 * buf:         buffer where read data will be stored
 * buflen:      maximum number of bytes that will be read from the file
 * 
 * returns:     number of bytes read from the file on success and -1 or error code on error
 */
int
read(int fd, userptr_t buf, size_t buflen, int *retval)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    
    struct open_file *of;

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        lock_release(curproc->oft->table_lock);
        return EBADF;
    } else {
        of = curproc->oft->table[fd];
    }

    if (of->flags & O_RDWR == 0 || of->flags & O_WRONLY != 0) {
        lock_release(curproc->oft->table_lock);
        return EACCES;
    }
    lock_release(curproc->oft->table_lock);
    
    int result;
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *myuio = kmalloc(sizeof(struct uio));
    
    lock_acquire(of->flock);
    uio_uinit(iov, myuio, buf, buflen, of->offset, UIO_READ);

    result = VOP_READ(of->vn, myuio);
    if (result) {
        lock_release(of->flock);
        return result;
    }

    *retval = myuio->uio_offset - of->offset;
    of->offset = myuio->uio_offset;
    lock_release(of->flock);

    return 0;
}

/*
 * write data to file 
 * -------------------
 *
 * fd:          file descriptor of file to read from
 * buf:         buffer of data to write to file from
 * nbytes:      max number of bytes to write to file
 * 
 * returns:     number of bytes written to file on success and -1 or error code on error
 */
int
write(int fd, userptr_t buf, size_t nbytes, int *retval) 
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    
    struct open_file *of;

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[fd] == NULL) {
        lock_release(curproc->oft->table_lock);
        return EBADF;
    } else {
        of = curproc->oft->table[fd];
    }
    
    if (of->flags & O_RDWR == 0 && of->flags & O_WRONLY == 0) {
        lock_release(curproc->oft->table_lock);
        return EACCES;
    }
    lock_release(curproc->oft->table_lock);

    int result;
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *myuio = kmalloc(sizeof(struct uio));

    lock_acquire(of->flock);
    uio_uinit(iov, myuio, buf, nbytes, of->offset, UIO_WRITE);

    result = VOP_WRITE(of->vn, myuio);
    if (result) {
        lock_release(of->flock);
        return result;
    }
    
    *retval = myuio->uio_offset - of->offset;
    of->offset = myuio->uio_offset;
    lock_release(of->flock);

    return 0;
}

/*
 * change current position in file
 * -------------------------------
 *
 * fd:          file descriptor of file to read from
 * pos:         buffer of data to write to file from
 * whence:      max number of bytes to write to file
 * 
 * returns:     number of bytes written to file stored in retval and retval_v1 on success
 *              returns -1 or error code on error
 */
int
lseek(int fd, off_t pos, int whence, uint32_t *retval, uint32_t *retval_v1)
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
            split64to32(of->offset, (uint32_t *)retval, (uint32_t *)retval_v1);
            break;

        case SEEK_CUR:
            of->offset = of->offset + pos;
            split64to32(of->offset, (uint32_t *)retval, (uint32_t *)retval_v1);
            break;

        case SEEK_END: ;
            struct stat *statbuf = kmalloc(sizeof(struct stat));
            int result = VOP_STAT(of->vn, statbuf);
            if (result) {
                return result;
            }
            of->offset = statbuf->st_size + pos;
            kfree(statbuf);
            split64to32((uint64_t) of->offset, retval, retval_v1);
            break;
            
        default: 
            return EINVAL;
    }

    return 0;
}

int
dup2(int oldfd, int newfd, int *retval)
{
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }

    lock_acquire(curproc->oft->table_lock);
    if (curproc->oft->table[oldfd] == NULL) {
        lock_release(curproc->oft->table_lock);
        return EBADF;
    }
    if (curproc->oft->table[newfd] != NULL) {
        open_file_decref(curproc->oft->table[newfd]);
    }
    curproc->oft->table[newfd] = curproc->oft->table[oldfd];
    open_file_incref(curproc->oft->table[oldfd]);
    lock_release(curproc->oft->table_lock);

    *retval = newfd;
    return 0;
}
/*
 * changes current directory
 * -------------------------
 * 
 * pathname:    directory to set current process to
 * 
 * returns:     0 on success, -1 on error
 */
int
chdir(const_userptr_t pathname)
{
    int result;

    char *kern_pathname;
    kern_pathname = kmalloc(PATH_MAX);
    if (kern_pathname == NULL) {
        return ENOSPC;
    }

    result = copyinstr(pathname, kern_pathname, PATH_MAX, NULL);
    if (result) {
        kfree(kern_pathname);
        return result;
    }

    result = vfs_chdir(kern_pathname);

    kfree(kern_pathname);
    return result;
}

/*
 * get name of current working directory (backend)
 * -----------------------------------------------
 * 
 * buf:         stores the current directory
 * buflen:      length of data stored in buf
 * 
 * returns:     length of data returned on success, -1 or error code on error
 */
int
__getcwd(userptr_t buf, size_t buflen, int *retval)
{
    int result;
    struct iovec *iov = kmalloc(sizeof(struct iovec));
    struct uio *myuio = kmalloc(sizeof(struct uio));
    
    uio_uinit(iov, myuio, buf, buflen, 0, UIO_READ);

    result = vfs_getcwd(myuio);

    *retval = myuio->uio_offset;
    return result;
}
