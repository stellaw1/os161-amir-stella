/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 10
#define NROPES 20
static int ropes_left = NROPES;

/* Data structures for rope mappings */

struct rope {
	struct lock *ropeLock;			// protects status of rope
	bool state; 					// false == attached; true == severed
};

struct stake {
	struct lock *stakeLock;			// protects ropeIndex that stake connects to
	int ropeIndex;					// index of rope that is attached to this stake
};

struct hook {
	int ropeIndex;					// index of rope that is attached to this stake
};

static struct rope ropes[NROPES];
static struct stake stakes[NROPES];
static struct hook hooks[NROPES];

/* Synchronization primitives */
static struct lock *ropesLeftLock;
static struct cv *ropesLeftCv;		// tracks whether balloon is free to escape ie all ropes are severed
static struct semaphore *doneSem;	// tracks completion status of all threads

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

// Helper functions
static
void
init()
{
	// init ropes, stakes, and hooks
	for (int i = 0 ; i < NROPES ; i++) {
		ropes[i].state = false;
		ropes[i].ropeLock = lock_create("ropeLock");
		if (ropes[i].ropeLock == NULL) {
			panic("Could not create ropeLock\n");
		}

		stakes[i].ropeIndex = i;
		stakes[i].stakeLock = lock_create("stakeLock");
		if (stakes[i].stakeLock == NULL) {
			panic("Could not create stakeLock\n");
		}

		hooks[i].ropeIndex = i;
	}

	// init number of ropes left to be severed
	ropes_left = NROPES;

	// init ropes left lock & cv
	ropesLeftLock = lock_create("ropes left");
	if (ropesLeftLock == NULL) {
		panic("Could not create ropesLeftLock\n");
	}
	ropesLeftCv = cv_create("balloon free");
	if (ropesLeftCv == NULL) {
		panic("Could not create ropesLeftCv\n");
	}

	//init semaphore
	doneSem = sem_create("main done", 0);
	if (doneSem == NULL) {
		panic("Could not create doneSem\n");
	}
}


//TODO

// static
// void
// cleanup()
// {
// 	for (int i = 0; i < NROPES; i++) {
// 		lock_destroy(ropeLocks[i]);
// 		ropeLocks[i] = NULL;
// 	}
// 	lock_destroy(ropesLeftLock);
// 	cv_destroy(ropesLeftCv);
// 	sem_destroy(doneSem);
// }

static
void
decrementRopesLeft()
{
	lock_acquire(ropesLeftLock);
	ropes_left--;
	if (ropes_left <= 0) {
		cv_broadcast(ropesLeftCv, ropesLeftLock);
	}
	lock_release(ropesLeftLock);
}


// Thread functions
static
void
dandelion(void *p, unsigned long arg) // sky hooks
{
	(void)p;
	(void)arg;
	int ropeIndex, hookIndex;

	kprintf("Dandelion thread starting\n");

	while (ropes_left > 0) {
		hookIndex = random() % NROPES;
		ropeIndex = hooks[hookIndex].ropeIndex;

		lock_acquire(ropes[ropeIndex].ropeLock);
		if (ropes_left > 0 && ropes[ropeIndex].state == false) {
			ropes[ropeIndex].state = true;
			kprintf("Dandelion severed rope %d\n", ropeIndex);

			decrementRopesLeft();
			thread_yield();
		}
		lock_release(ropes[ropeIndex].ropeLock);
	}

	kprintf("Dandelion thread done\n");
	V(doneSem);
}

static
void
marigold(void *p, unsigned long arg) // ground stakes
{
	(void)p;
	(void)arg;
	int ropeIndex, stakeIndex;

	kprintf("Marigold thread starting\n");

	while (ropes_left > 0) {
		stakeIndex = random() % NROPES;

		lock_acquire(stakes[stakeIndex].stakeLock);
		ropeIndex = stakes[stakeIndex].ropeIndex;

		lock_acquire(ropes[ropeIndex].ropeLock);
		if (ropes_left > 0 && ropes[ropeIndex].state == false) {
			ropes[ropeIndex].state = true;
			kprintf("Marigold severed rope %d from stake %d \n", ropeIndex, stakeIndex);

			decrementRopesLeft();
			thread_yield();
		}
		lock_release(ropes[ropeIndex].ropeLock);
		lock_release(stakes[stakeIndex].stakeLock);
	}

	kprintf("Marigold thread done\n");
	V(doneSem);
}

// static
// void
// flowerkiller(void *p, unsigned long arg)
// {
// 	(void)p;
// 	(void)arg;
// 	int stakeK, stakeP, ropeK, ropeP;

// 	kprintf("Lord FlowerKiller thread starting\n");

// 	while (ropes_left > 1) {

// 		lock_acquire(ropesLeftLock);
// 		if (ropes_left > 1) {
// 			lock_release(ropesLeftLock);

// 			stakeK = random() % NROPES;
// 			stakeP = random() % NROPES;

// 			if (stakeK < stakeP) {
// 				lock_acquire(ropeLocks[stakes[stakeK]]);
// 				lock_acquire(ropeLocks[stakes[stakeP]]);
// 			} else if (stakeP < stakeK) {
// 				lock_acquire(ropeLocks[stakes[stakeP]]);
// 				lock_acquire(ropeLocks[stakes[stakeK]]);
// 			} else {
// 				continue;
// 			}

// 			if (ropes[stakes[stakeK]] || ropes[stakes[stakeP]]) {
// 				lock_release(ropeLocks[stakes[stakeK]]);
// 				lock_release(ropeLocks[stakes[stakeP]]);

// 				continue;
// 			}

// 			ropeK = stakes[stakeK];
// 			ropeP = stakes[stakeP];

// 			stakes[stakeK] = ropeP;
// 			stakes[stakeP] = ropeK;

// 			lock_release(ropeLocks[stakes[stakeK]]);
// 			lock_release(ropeLocks[stakes[stakeP]]);

// 			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d \n", ropeK, stakeK, stakeP);
// 			thread_yield();
// 		} else {
// 			lock_release(ropesLeftLock);
// 		}
// 	}

// 	kprintf("Lord FlowerKiller thread done\n");
// 	V(doneSem);
// }

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	lock_acquire(ropesLeftLock);
	while (ropes_left > 0) {
		cv_wait(ropesLeftCv, ropesLeftLock);
		lock_release(ropesLeftLock);
	}
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");
	V(doneSem);
}


int
airballoon(int nargs, char **args)
{

	int err = 0;
	int nthreads = 3;

	(void)nargs;
	(void)args;
	(void)ropes_left;


	init();


	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	// for (int i = 0; i < N_LORD_FLOWERKILLER; i++) {
	// 	err = thread_fork("Lord FlowerKiller Thread",
	// 			  NULL, flowerkiller, NULL, 0);
	// 	if(err)
	// 		goto panic;
	// }

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;


	// thread_join()
	for (int i = 0; i < nthreads; i++) {
		P(doneSem);
	}
	kprintf("Main thread done\n");

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	// cleanup();
	return 0;
}
