/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 50
#define NROPES 100
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
static struct lock *ropesLeftLock;	// protects the ropes_left variable
static struct cv *balloonFreeCv;	// tracks whether balloon is free to escape ie, all ropes are severed
static struct semaphore *doneSem;	// tracks completion status of all threads


/* Helper functions */

/*
 * initializes ropes array to have state == false, and stakes/hooks to have 1-1 rope mapping
 * also initializes synchronization primitives for ropes, stakes, ropes_left and balloon free
 */
static
void
init()
{
	// init ropes, stakes, and hooks
	for (int i = 0 ; i < NROPES ; i++) {
		ropes[i].state = false; //attached
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
	balloonFreeCv = cv_create("balloon free");
	if (balloonFreeCv == NULL) {
		panic("Could not create balloonFreeCv\n");
	}

	//init thread_join semaphore
	doneSem = sem_create("main done", 0);
	if (doneSem == NULL) {
		panic("Could not create doneSem\n");
	}
}


/*
 * Cleans up memory used for synchronization primitives
 */
static
void
cleanup()
{
	for (int i = 0; i < NROPES; i++) {
		lock_destroy(ropes[i].ropeLock);
		ropes[i].ropeLock = NULL;
		lock_destroy(stakes[i].stakeLock);
		stakes[i].stakeLock = NULL;
	}
	lock_destroy(ropesLeftLock);
	cv_destroy(balloonFreeCv);
	sem_destroy(doneSem);
}


/*
 * Atomically decrements ropes_left by 1 and signals balloonFreeCv if ropes_left is 0
 */
static
void
decrementRopesLeft()
{
	lock_acquire(ropesLeftLock);
	ropes_left--;
	if (ropes_left <= 0) {
		cv_broadcast(balloonFreeCv, ropesLeftLock);
	}
	lock_release(ropesLeftLock);
}


/* Thread functions */

/*
 * Dandelion accesses hooks array with some random hookIndex in the range [0, NROPES)
 * It grabs the ropeIndex at that hookIndex and grabs the according ropeLock at ropeIndex in ropes,
 * and marks the rope as severed if its state is false, moves on otherwise.
 */
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


/*
 * Marigold accesses stakes array with some random stakeIndex in the range [0, NROPES)
 * It grabs the lock and the ropeIndex at that stakeIndex and grabs the according ropeLock at ropeIndex in ropes,
 * and marks the rope as severed if its state is false, moves on otherwise.
 */
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


/*
 * Flowerkiller accesses stakes array with two random stakeK and stakeP in the range [0, NROPES) that are different
 * It grabs the two stakeLock's in ascending stakeIndex order then grabs both ropeIndex's and their ropeLock's
 * If the two ropes are not already severed, Flowerkiller switches the ropeIndex's on stakeK and stakeP, if either of
 * stakeK or stakeP has already been severed, we move on.
 */
static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int stakeK, stakeP, ropeK, ropeP;

	kprintf("Lord FlowerKiller thread starting\n");

	while (ropes_left > 1) {

		stakeK = random() % NROPES;
		stakeP = random() % NROPES;

		// check if stakeK and stakeP are the same stake
		if (stakeK == stakeP) {
			continue;
		}

		// grab stakeLock's in ascending order
		if (stakeK < stakeP) {
			lock_acquire(stakes[stakeK].stakeLock);
			lock_acquire(stakes[stakeP].stakeLock);
		} else {
			lock_acquire(stakes[stakeP].stakeLock);
			lock_acquire(stakes[stakeK].stakeLock);
		}

		// get both ropeIndex's
		ropeK = stakes[stakeK].ropeIndex;
		ropeP = stakes[stakeP].ropeIndex;

		// grab both ropeLock's
		lock_acquire(ropes[ropeK].ropeLock);
		lock_acquire(ropes[ropeP].ropeLock);

		// check if either ropeK or ropeP has been severed already
		if (ropes[ropeK].state || ropes[ropeP].state) {
			lock_release(ropes[ropeK].ropeLock);
			lock_release(ropes[ropeP].ropeLock);

			lock_release(stakes[stakeK].stakeLock);
			lock_release(stakes[stakeP].stakeLock);

			continue;
		}

		// swap ropeK and ropeP
		stakes[stakeK].ropeIndex = ropeP;
		stakes[stakeP].ropeIndex = ropeK;

		lock_release(ropes[ropeK].ropeLock);
		lock_release(ropes[ropeP].ropeLock);

		lock_release(stakes[stakeK].stakeLock);
		lock_release(stakes[stakeP].stakeLock);

		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d \n", ropeK, stakeK, stakeP);
		thread_yield();
	}

	kprintf("Lord FlowerKiller thread done\n");
	V(doneSem);
}


/*
 * Balloon waits on balloonFreeCv which signals that all ropes have been severed and balloon is free.
 */
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	lock_acquire(ropesLeftLock);
	while (ropes_left > 0) {
		cv_wait(balloonFreeCv, ropesLeftLock);
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
	int nthreads = N_LORD_FLOWERKILLER + 3;

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

	for (int i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;


	// waits for nthreads to finish before moving on
	for (int i = 0; i < nthreads; i++) {
		P(doneSem);
	}
	kprintf("Main thread done\n");

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	cleanup();
	return 0;
}
