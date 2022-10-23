/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * SYNCHRONIZATION PROBLEM 2: AIR BALLOON
 *
 * After a war erupts in their kingdom, Princess Marigold must help
 * Prince Dandelion (her younger brother) escape from danger. Marigold places
 * Dandelion in a hot air balloon, which is connected to the ground by
 * NROPES ropes -- each rope is connected to a hook on the balloon as well as
 * a stake in the ground. Marigold and Dandelion must work together to sever all
 * of these ropes so that Dandelion can escape. Marigold unties the ropes from
 * the ground stakes while Dandelion unhooks the ropes from the balloon.
 *
 * Unfortunately, one of Princess Marigold and Prince Dandelion's enemies,
 * Lord FlowerKiller, is also at work. FlowerKiller is rearranging the ropes
 * to thwart Princess Marigold and Prince Dandelion. He will randomly unhook
 * a rope from one stake and move it to another stake. This leads to chaos!
 *
 * Without Lord FlowerKiller's dastardly, behavior, there would be a simple
 * 1:1 correspondence between balloon_hooks and ground_stakes (each hook in
 * balloon_hooks has exactly one corresponding entry in ground_stakes, and
 * each stake in ground_stakes has exactly one corresponding entry in
 * balloon_hooks). However, while Lord FlowerKiller is around, this perfect
 * 1:1 correspondence may not exist.
 *
 * As Marigold and Dandelion cut ropes, they must delete mappings, so that they
 * remove all the ropes as efficiently as possible (that is, once Marigold has
 * severed a rope, she wants to communicate that information to Dandelion, so
 * that he can work on severing different ropes). They will each use NTHREADS
 * to sever the ropes and udpate the mappings. Dandelion selects ropes to sever
 * by generating a random balloon_hook index, and Marigold selects ropes by
 * generating a random ground_stake index.
 *
 * Lord FlowerKiller has only a single thread. He is on the ground, so like
 * Marigold, he selects ropes by their ground_stake index.
 *
 * Consider this example:
 * Marigold randomly selects the rope attached to ground_stake 7 to sever. She
 * consults the mapping for ground_stake 7, sees that it is still mapped, and
 * sees that the other end of the rope attaches to balloon_hook 11. To cut the
 * rope, she must free the mappings in both ground_stake 7 and balloon_hook 11.
 * Imagine that Dandelion randomly selects balloon_hook index 11 to delete. He
 * determines that it is still mapped, finds that the corresponding ground_stake
 * index is 7. He will want to free the mappings in balloon_hook 11 and
 * ground_stake 7. It's important that Dandelion and Marigold don't get in each
 * other's way. Worse yet, Lord FlowerKiller might be wreaking havoc with the
 * same ropes.  For example, imagine that he decides to swap ground_stake 7
 * with ground_stake 4 at the same time.  Now, all of a sudden, balloon_hook 11
 * is no longer associated with ground_stake 7 but with ground_stake 4.
 *
 * Without proper synchronization, Marigold and Dandelion can encounter:
 * - a race condition, where multiple threads attempt to sever the same rope at
 *   the same time (e.g., two different Marigold threads attempt to sever the
 *   rope attached to ground_stake 7).
 * - a deadlock, where two threads select the same rope, but accessing it from
 *   different directions (e.g., Dandelion gets at the rope from balloon_hook 11
 *   while Marigold gets at the rope from ground_stake 7).
 *
 * Your solution must satisfy these conditions:
 *  - Avoid race conditions.
 *  - Guarantee no deadlock can occur. Your invariants and comments should
 *  provide a convincing proof of this.
 *  HINT: This includes ensuring that Lord FlowerKiller's behavior does not
 *  cause any race conditions or deadlocks by adding the appropriate
 *  synchronization to his thread as well.
 *  HINT: You should insert well-placed thread_yield() calls in your code to
 *  convince yourself that your synchronization is working.
 *  - When Marigold and Dandelion select ropes to cut, you may choose to ignore
 *    a particular choice and generate a new one, however, all mappings must
 *    eventually be deleted.
 *  HINT: Use this to your advantage to introduce some asymmetry to the
 *  problem.
 *  - Permit multiple Marigold/Dandelion threads to sever ropes concurrently
 *  (no "big lock" solutions)
 *
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
			kprintf("Marigold severed rope %d from stake %d\n", ropeIndex, stakeIndex);

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

		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\nLord FlowerKiller switched rope %d from stake %d to stake %d\n", 
		ropeK, stakeK, stakeP, ropeP, stakeP, stakeK);
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
