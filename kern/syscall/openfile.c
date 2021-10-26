#include <types.h>
#include <openfiletable.h>
#include <vfs.h>
#include <synch.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/errno.h>


/*
 * Generates a new open file entry
 *
 * returns:     pointer to newly generated open_file structure on success and 
 *              NULL on error
 */
struct open_file *
open_file_create(struct vnode *vn, int flag)
{
    struct open_file *of;
    of = kmalloc(sizeof(struct open_file));
    if (of == NULL) {
        return NULL;
    }

    of->flock = lock_create("filelock");
    if (of->flock == NULL) {
        kfree(of);
        return NULL;
    }

    of->vn = vn;
    of->flag = flag;
    of->offset = 0;
    of->refcount = 1;

    return of;
}

/*
 * Destroys and cleans up open file entry
 */
void
open_file_destroy(struct open_file *of)
{
    vfs_close(of->vn);

    lock_destroy(of->flock);
    kfree(of);
}

/*
 * Increases the open file reference count by 1
 */
void 
open_file_incref(struct open_file *of) 
{
    lock_acquire(of->flock);
    of->refcount++;
    lock_release(of->flock);
}

/*
 * Decreases open file reference count by and destroys the entry if 0
 */
void
open_file_decref(struct open_file *of) 
{
    lock_acquire(of->flock);
    if (of->refcount <= 1) {
        lock_release(of->flock);
        open_file_destroy(of);
    } else {
        of->refcount--;
        lock_release(of->flock);
    }
}
