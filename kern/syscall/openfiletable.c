#include <types.h>
#include <openfiletable.h>
#include <openfile.h>
#include <vfs.h>
#include <synch.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/fcntl.h>


/*
 * Generates a new open file table structure
 */
struct open_file_table *
open_file_table_create()
{
    struct open_file_table *oft;
    oft = kmalloc(sizeof(struct open_file_table));
    if (oft == NULL) {
        return NULL;
    }

    oft->table_lock = lock_create("filetablelock");
    if (oft->table_lock == NULL) {
        kfree(oft);
        return NULL;
    }

    for (int fd = 0; fd < OPEN_MAX; fd++) {
        oft->table[fd] = NULL;
    }

    return oft;
}

/*
 * Generates special file descriptors 0, 1, and 2 that are used for stdin,
 * stdout, and stderr
 *
 * returns:     0 on success and an errcode or -1 otherwise
 */
int
special_fd_create(struct open_file_table *oft)
{
    for (int fd = 0; fd < 3; fd++) {
        char console_path[32];
        struct vnode *vn;
        struct open_file *of;
        int result;

	    strcpy(console_path, "con:");

        if (fd == STDIN_FILENO) {
            result = vfs_open(console_path, O_RDONLY, 0664, &vn);
            if (result) {
                return result;
            }
            of = open_file_create(vn, O_RDONLY);
            if (of == NULL) {
                return -1;
            }
            oft->table[STDIN_FILENO] = of;

        } else if (fd == STDOUT_FILENO) {
            result = vfs_open(console_path, O_WRONLY, 0664, &vn);
            if (result) {
                return result;
            }
            of = open_file_create(vn, O_WRONLY);
            if (of == NULL) {
                return -1;
            }
            oft->table[STDOUT_FILENO] = of;

        } else if (fd == STDERR_FILENO) {
            result = vfs_open(console_path, O_WRONLY, 0664, &vn);
            if (result) {
                return result;
            }
            of = open_file_create(vn, O_WRONLY);
            if (of == NULL) {
                return -1;
            }
            oft->table[STDERR_FILENO] = of;
        }
    }
    return 0;
}

/*
 * Cleans up open file table
 */
void
open_file_table_destroy(struct open_file_table *oft) {
    if (oft == NULL) {
        return;
    }

    lock_destroy(oft->table_lock);
    for (int fd = 0; fd < OPEN_MAX; fd++) {
        if (oft->table[fd] != NULL) {
            open_file_decref(oft->table[fd]);
            oft->table[fd] = NULL;
        }
    }
}

/*
 * Copies non special fd's from old_oft to new_oft
 */
int
open_file_table_copy(struct open_file_table *old_oft, struct open_file_table *new_oft) {
    if (old_oft == NULL || new_oft == NULL) {
        return -1;
    }

    lock_acquire(old_oft->table_lock);
    lock_acquire(new_oft->table_lock);
    for (int fd = 0; fd < OPEN_MAX; fd++) {
        if (old_oft->table[fd] != NULL) {
            //assign any non NULL fd pointers to new_oft at the same index
            new_oft->table[fd] = old_oft->table[fd];

            // increase refcount of open file entry by 1
            open_file_incref(old_oft->table[fd]);
        }
    }
    lock_release(new_oft->table_lock);
    lock_release(old_oft->table_lock);

    return 0;
}
