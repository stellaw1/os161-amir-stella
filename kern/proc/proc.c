/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <openfiletable.h>
#include <openfile.h>
#include <synch.h>
#include <limits.h>
#include <kern/errno.h>
#include <pid.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Global variables
 */
struct pid *pid_table[PID_MAX];
struct lock *pid_table_lock;


/*
 * pid entry is destoryed and the slot can be reused after
 */
void
destroy_pid_entry(pid_t pidIndex)
{
	lock_acquire(pid_table_lock);

	if (pid_table[pidIndex] != NULL){
		sem_destroy(pid_table[pidIndex]->exitLock);
		kfree(pid_table[pidIndex]);
		pid_table[pidIndex] = NULL;
	}
	lock_release(pid_table_lock);
}

/*
 * update exitFlag in pid entry at pidIndex
 */
void
set_pid_exitFlag(pid_t pidIndex, bool exitFlag)
{
	lock_acquire(pid_table_lock);
	pid_table[pidIndex]->exitFlag = exitFlag;
	lock_release(pid_table_lock);
}

/*
 * gets exitFlag in pid entry at pidIndex
 */
bool
get_pid_exitFlag(pid_t pidIndex)
{
	lock_acquire(pid_table_lock);
	KASSERT(pid_table[pidIndex] != NULL);

	bool ret = pid_table[pidIndex]->exitFlag;
	lock_release(pid_table_lock);

	return ret;
}

/*
 * update exitStatus in pid entry at pidIndex
 */
void
set_pid_exitStatus(pid_t pidIndex, int exitStatus)
{
	lock_acquire(pid_table_lock);
	pid_table[pidIndex]->exitStatus = exitStatus;
	lock_release(pid_table_lock);
}

/*
 * get exitStatus in pid entry at pidIndex
 */
int
get_pid_exitStatus(pid_t pidIndex)
{
	lock_acquire(pid_table_lock);
	KASSERT(pid_table[pidIndex] != NULL);

	int ret = pid_table[pidIndex]->exitStatus;
	lock_release(pid_table_lock);

	return ret;
}

/*
 * returns boolean of whether or not pidIndex is valid and has a pid entry at that index
 */
bool
get_pid_in_table(pid_t pidIndex)
{
	if (pidIndex < PID_MIN || pidIndex >= PID_MAX) {
		return false;
	}

	lock_acquire(pid_table_lock);
	bool ret = pid_table[pidIndex] != NULL;
	lock_release(pid_table_lock);

	return ret;
}

/*
 * get parentPid in pid entry at pidIndex
 */
pid_t
get_parent_pid(pid_t pidIndex)
{
	lock_acquire(pid_table_lock);
	KASSERT(pid_table[pidIndex] != NULL);

	pid_t ret = pid_table[pidIndex]->parentPid;
	lock_release(pid_table_lock);

	return ret;
}

/*
 * get exitLock in pid entry at pidIndex
 */
struct semaphore*
get_exitLock(pid_t pidIndex)
{
	lock_acquire(pid_table_lock);
	KASSERT(pid_table[pidIndex] != NULL);

	struct semaphore* ret = pid_table[pidIndex]->exitLock;
	lock_release(pid_table_lock);

	return ret;
}



/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);

	open_file_table_destroy(proc->oft);

	// clean up pid entry, childProcs array and childProcsLock
	if (proc->pid) {
    	destroy_pid_entry(proc->pid);
	}

	if (proc->childProcs) {
    	array_destroy(proc->childProcs);
	}

	if (proc->childProcsLock) {
    	lock_destroy(proc->childProcsLock);
	}
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}

	pid_table_lock = lock_create("pid_table_lock");
	if (pid_table_lock == NULL) {
		panic("failed to create pid table lock\n");
	}

	kproc->pid = 1;
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	/*
	 * Generate open file table strucutre and instantiate special fd 0, 1, 2
	 * for new process
	 */
	int result;
	newproc->oft = open_file_table_create();
	if (newproc->oft == NULL) {
		proc_destroy(newproc);
		return NULL;
	}
	result = special_fd_create(newproc->oft);
	if (result) {
		return NULL;
	}

	// grab lock and create pid entry for the new process
	lock_acquire(pid_table_lock);
	for (int i = PID_MIN; i < PID_MAX + 1; i++) {
		if (pid_table[i] == NULL) { // found empty slot in pid table
			pid_table[i] = kmalloc(sizeof(struct pid));
			if (pid_table[i] == NULL) {
				return 0;
			}
			pid_table[i]->exitFlag = false;
			pid_table[i]->exitLock = sem_create("exitLock", 0);
			pid_table[i]->parentPid = curproc->pid;

			newproc->pid = i;
			break;
		} else if (i == PID_MAX) { // too many processes in pid table
			lock_release(pid_table_lock);
			return NULL;
		}
	}
	lock_release(pid_table_lock);

	newproc->parentDead = false;
	newproc->childProcs = array_create();
	newproc->childProcsLock = lock_create("childProcsLock");

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
