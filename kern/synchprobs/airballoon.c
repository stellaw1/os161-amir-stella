/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 20
#define NROPES 30
static int ropes_left = NROPES;

/* Data structures for rope mappings */

static int hooks[NROPES]; 			// represents rope index that hooks are attached to and -1 represents unhooked hooks
static int stakes[NROPES];			// represents rope index that stakes are attached to and -1 represents unstaked stakes
static bool ropes[NROPES];			// represents status of rope - false == unsevered; true == severed

/* Synchronization primitives */

static struct lock *ropeLock; 		// protects hooks, stakes, and ropes_left
static struct cv *balloonCv;		// tracks whether balloon is free to escape ie all ropes are severed
static struct semaphore *doneSem;	// tracks completion status of all threads

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
init()
{
	// init rope positions
	for (int i = 0 ; i < NROPES ; i++) {
        hooks[i] = i;
        stakes[i] = i;
		ropes[i] = false;
	}

	// init number of ropes left to be severed
	ropes_left = NROPES;

	// init mutex lock
	ropeLock = lock_create("ropePositions");
	if (ropeLock == NULL) {
		panic("Could not create mutex_lock\n");
	}

	// init cv
	balloonCv = cv_create("balloon free");
	if (balloonCv == NULL) {
		panic("Could not create balloonCv\n");
	}

	//init semaphore
	doneSem = sem_create("main done", 0);
	if (doneSem == NULL) {
		panic("Could not create doneSem\n");
	}
}

static
void
cleanup()
{
	lock_destroy(ropeLock);
	cv_destroy(balloonCv);
	sem_destroy(doneSem);
	
	//TODO 
	// kfree(hooks);
	// kfree(stakes);
	// kfree(ropes);
	// kfree(ropes_left);
}

static
void
checkBalloonStatus()
{
	if (ropes_left <= 0) {
		cv_broadcast(balloonCv, ropeLock);
	}
}


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

		lock_acquire(ropeLock);
		if (ropes_left > 0) {
			ropeIndex = hooks[hookIndex];
			if (ropeIndex != -1) { // hook has not been unhooked
				if (ropes[ropeIndex] == false) { //rope is not yet severed 
					ropes[ropeIndex] = true;
					hooks[hookIndex] = -1;
					kprintf("Dandelion severed rope %d\n", hookIndex);

					ropes_left--;
					checkBalloonStatus();
				} else { //update hook to be "unhooked" as the attached rope has been unstaked
					hooks[hookIndex] = -1;
				}
				
			}
		}
		lock_release(ropeLock);
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
	int ropeIndex;

	kprintf("Marigold thread starting\n");

	while (ropes_left > 0) {
		int stakeIndex = random() % NROPES;

		lock_acquire(ropeLock);
		if (ropes_left > 0) {
			//TODO check ropes_left non zero
			ropeIndex = stakes[stakeIndex];
			if (ropeIndex != -1) { //stake has not been unstaked
				if (ropes[ropeIndex] == false) { //rope is not yet severed
					ropes[ropeIndex] = true;
					stakes[stakeIndex] = -1;
					kprintf("Marigold severed rope %d from stake %d \n", ropeIndex, stakeIndex);

					ropes_left--;
					checkBalloonStatus();
				} else { //update stake to be "unstaked" as the attached rope has been unhooked
					stakes[stakeIndex] = -1;
				}
			}
		}
		lock_release(ropeLock);
	}

	kprintf("Marigold thread done\n");
	V(doneSem);
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int stakeK, stakeP, ropeK, ropeP;

	kprintf("Lord FlowerKiller thread starting\n");

	while (ropes_left > 1) {

		lock_acquire(ropeLock);
		if (ropes_left > 1) {
			stakeK = random() % NROPES;
			ropeK = stakes[stakeK];
			
			while (ropeK == -1 || ropes[ropeK] == true) {
				stakeK = random() % NROPES;
				ropeK = stakes[stakeK];
			}

			stakeP = random() % NROPES;
			ropeP = stakes[stakeP];
			while (ropeP == -1 || ropes[ropeP] == true || stakeP == stakeK) {
				stakeP = random() % NROPES;
				ropeP = stakes[stakeP];
			}

			stakes[stakeK] = ropeP;
			stakes[stakeP] = ropeK;

			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d \n", ropeK, stakeK, stakeP);
		}
		lock_release(ropeLock);
	}
	
	kprintf("Lord FlowerKiller thread done\n");
	V(doneSem);
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	lock_acquire(ropeLock);
	while (ropes_left > 0) {
		cv_wait(balloonCv, ropeLock);
	}
	kprintf("Balloon freed and Prince Dandelion escapes!\n");

	lock_release(ropeLock);

	kprintf("Balloon thread done\n");
	V(doneSem);
}


int
airballoon(int nargs, char **args)
{

	int err = 0, i;
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


	//thread_join()
	for (i = 0; i < nthreads; i++) {
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
