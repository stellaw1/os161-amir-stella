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

int open(char *filename, int flags, mode_t mode) {

    struct vnode **ret;
    struct open_file *of;
    
    struct vnode *ptr1 = kmalloc(sizeof(struct vnode));
    if (ptr1 == NULL) {
        return -1;
    }
    
    ret = &ptr1;

    int result = vfs_open(filename, flags, mode, ret);
    
    kfree(ptr1);
        
    if (result != 0) {
        return -1;
    }

    of = open_file_create(*ret, 0, flags, 1);
    
    if (of == NULL) {
        return -1;
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
    return -1;
}
