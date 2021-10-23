#include <openfiletable.h>
#include <vfs.h>
#include <synch.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <limits.h>

struct open_file *
open_file_create(struct vnode *vn, off_t offset, int flags, int refcount)
{
    struct open_file *of = kmalloc(sizeof(struct open_file));
    if (of == NULL) {
        return NULL;
    }

    of->flock = lock_create("filelock");
    if (of->flock == NULL) {
        kfree(of);
        return NULL;
    }

    of->vn = vn;
    of->offset = offset;
    of->flags = flags;
    of->refcount = refcount;

    return of;
}

void
open_file_destroy(struct open_file *of)
{
    if (of != NULL) {
        kfree(of->flock);
        kfree(of);
    }
}

struct open_file_table *
open_file_table_create() 
{
    struct open_file_table *oft = kmalloc(sizeof(struct open_file_table));
    if (oft == NULL) {
        return NULL;
    }

    oft->table_lock = lock_create("filetablelock");
    if (oft->table_lock == NULL) {
        kfree(oft);
        return NULL;
    }
    
    char console_path[] = "con:";
    struct vnode **stdin_vn;
    struct vnode **stdout_vn;
    *stdin_vn = kmalloc(sizeof(struct vnode));
    if (*stdin_vn == NULL) {
        lock_destroy(oft->table_lock);
        kfree(oft);
        return NULL;
    }

    *stdout_vn = kmalloc(sizeof(struct vnode));
    if (*stdout_vn == NULL) {
        lock_destroy(oft->table_lock);
        kfree(oft);
        kfree(*stdin_vn);
        return NULL;
    }

    int err_stdin = vfs_open(console_path, O_RDONLY, 0, stdin_vn);
    int err_stdout = vfs_open(console_path, O_WRONLY, 0, stdout_vn);

    if (err_stdin != 0 || err_stdout != 0) {
        lock_destroy(oft->table_lock);
        kfree(oft);
        return NULL;
    }

    struct open_file *stdin_of = open_file_create(*stdin_vn, 0, O_RDONLY, 1);
    if (stdin_of == NULL) {
        lock_destroy(oft->table_lock);
        kfree(oft);
        return NULL;
    }

    struct open_file *stdout_of = open_file_create(*stdout_vn, 0, O_WRONLY, 1);
    if (stdout_of == NULL) {
        open_file_destroy(stdin_of);
        lock_destroy(oft->table_lock);
        kfree(oft);
        return NULL;
    }

    struct open_file *stderr_of = open_file_create(*stdout_vn, 0, O_WRONLY, 1);
    if (stderr_of == NULL) {
        open_file_destroy(stdin_of);
        open_file_destroy(stdout_of);
        lock_destroy(oft->table_lock);
        kfree(oft);
        return NULL;
    }

    oft->table[STDIN_FILENO] = stdin_of;
    oft->table[STDOUT_FILENO] = stdout_of;
    oft->table[STDERR_FILENO] = stderr_of;

    return oft;
}

void
open_file_table_destroy(struct open_file_table *oft) {
    if (oft == NULL) {
        return;
    }

    lock_destroy(oft->table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (oft->table[i] == NULL) {
            continue;
        }
        open_file_destroy(oft->table[i]);
    }
}
