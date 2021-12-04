#include <types.h>
#include <machine/vm.h>
#include <addrspace.h>


/*
 * coremap definitions
 */
struct coremap_entry *coremap;                                      /* global coremap pointer */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;        /* coremap lock */

int totalpages;


/*
 * coremap entries definition
 */
struct coremap_entry {
	paddr_t address; // virtual page number; might need to make this an array to store myltiple virtual addresses
	vaddr_t virtual_addr; // virtual page number; might need to make this an array to store myltiple virtual addresses
	// struct addrspace *mapped_space;

	int numpages;

	// flags
	bool busyFlag;
};
