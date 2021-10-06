/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */

static int hooks[NROPES];
static int stakes[NROPES];

/* Synchronization primitives */

static struct lock *mutexLock; // protects hooks, stakes, and ropes_left
// static struct cv *balloonFreeCv;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
initRopePositions()
{
	for (int i = 0 ; i < NROPES ; i++) {
        hooks[i] = i;
        stakes[i] = i;
	}
}


static
void
cleanup()
{
	lock_destroy(mutexLock);
}


// static
// int
// getIndexOf(int array[NROPES], int ropeIndex)
// {
// 	for (int i = 0; i < NROPES; i++) {
// 		if (array[i] == ropeIndex) {
// 			return i;
// 		}
// 	}
// 	return -1;
// }


static
void
dandelion(void *p, unsigned long arg) // sky hooks
{
	(void)p;
	(void)arg;
	int ropeIndex;

	kprintf("Dandelion thread starting\n");

	while (ropes_left > 0) {
		int hookIndex = random() % NROPES;

		lock_acquire(mutexLock);
		ropeIndex = hooks[hookIndex];
		if (ropeIndex != -1) {
			hooks[hookIndex] = -1;
			kprintf("Dandelion severed rope %d\n", hookIndex);

			ropes_left--;
		}
		lock_release(mutexLock);
	}

	kprintf("Dandelion thread done\n");
	thread_exit();
}

static
void
marigold(void *p, unsigned long arg) // ground stakes 
{
	(void)p;
	(void)arg;
	int ropeIndex;

	kprintf("Marigold thread starting\n");

	while (ropes_left > 0) {
		int stakeIndex = random() % NROPES;
		kprintf("%d", ropeIndex);

		lock_acquire(mutexLock);
		ropeIndex = stakes[stakeIndex];
		if (ropeIndex != -1) {
			stakes[stakeIndex] = -1;
			kprintf("Marigold severed rope %d from stake %d\n", ropeIndex, stakeIndex);

			ropes_left--;
		}
		lock_release(mutexLock);
	}

	kprintf("Marigold thread done\n");
	thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	thread_exit();
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;


	initRopePositions();
	mutexLock = lock_create("ropePositions");

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	cleanup();
	return 0;
}
