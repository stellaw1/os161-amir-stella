#include <types.h>
#include <machine/vm.h>
#include <addrspace.h>


bool coremap_initialized = false;


/*
 * coremap definitions
 */
struct coremap_entry *coremap;       	/* global coremap pointer that acts as an array of struct coremap_entry's */
paddr_t coremap_addr;					/* physical address of coremap */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;         /* coremap lock */
int totalpages;							/* number of entries in coremap */


/*
 * coremap entries definition
 */
struct coremap_entry {
	vaddr_t virtual_addr; 				/* virtual page number; might need to make this an array to store myltiple virtual addresses */
	int segment_pages;						/* number of consecutive pages allocated to this segment */

	bool busyFlag;						/* true if this page has been allocated, false if not */
};
