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

int open(const char *filename, int flags, mode_t mode) {

    struct vnode **ret;
    struct open_file *of;
    int result;

    of = kmalloc(sizeof(struct open_file));

    result = vfs_open(filename, flags, mode, ret);

    of->vn = ret;
    of->offset = 0;
    of->flags = flags;

    lock_acquire(curproc->oft->table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->oft[i] == NULL) {
            curproc->oft[i] = of;
            lock_release(curproc->oft->table_lock);
            return result;
        }
    }
    lock_release(curproc->oft->table_lock);
    return EMFILE;
}