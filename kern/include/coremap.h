#include <types.h>
#include <machine/vm.h>
#include <addrspace.h>


bool coremap_initialized = false;


/*
 * coremap definitions
 */
struct coremap_entry *coremap;                                      /* global coremap pointer */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;        /* coremap lock */

paddr_t coremap_addr;
int totalpages;


/*
 * coremap entries definition
 */
struct coremap_entry {
	// paddr_t address; // virtual page number; might need to make this an array to store myltiple virtual addresses
	vaddr_t virtual_addr; // virtual page number; might need to make this an array to store myltiple virtual addresses

	int numpages;

	// flags
	bool busyFlag;
};
