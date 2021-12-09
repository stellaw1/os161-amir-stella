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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>

/*
 * Smarter implementation of VM
 */

#define PAGE_SIZE    		4096



/*
 * Helper functions
 *********************/


/* 
 * helper function for allocating kernel pages after coremap has been initialized
 */
static vaddr_t post_vm_init_alloc(unsigned npages) {
	paddr_t addr;

	spinlock_acquire(&coremap_lock);

	int alloced = 0;
	int start = -1;

	// iterate through coremap until npages of consecutive free pages have been found
	for (int i = 0; i < totalpages; i++) {
		if (!coremap[i].busyFlag && start == -1) {
			alloced++;
			start = i;
		} else if (!coremap[i].busyFlag) {
			alloced++;
		} else {
			alloced = 0;
			start = -1;
		}

		// if enough consecutive free pages have been found, break out of loop
		if (alloced == (int) npages) {
			break;
		}
	}

	// return 0 if coremap is full
	if (alloced < (int) npages) {
		spinlock_release(&coremap_lock);
		return 0;
	}

	// set return address to physical address of first page in consecutive segment
	addr = coremap_addr + (start * PAGE_SIZE);
	// initialize the segment
	as_zero_region(addr, npages);

	// translate physical return address to a virtual address
	vaddr_t ret = PADDR_TO_KVADDR(addr);

	// set properties in coremap to indicate pages as busy
	coremap[start].segment_pages = npages;
	coremap[start].virtual_addr = ret;

	for (int i = 0; i < (int) npages; i++) {
		coremap[i + start].busyFlag = true;
	}

	spinlock_release(&coremap_lock);

	return ret;
}

/* 
 * helper function for allocating kernel pages before coremap has been initialized
 */
static vaddr_t pre_vm_init_alloc(unsigned npages) {
	paddr_t addr;

	spinlock_acquire(&coremap_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&coremap_lock);
	if (addr == 0) {
		return 0;
	}

	return PADDR_TO_KVADDR(addr);
}

/*
 * helper function that initializes npages pages of memory starting at paddr
 */
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}





/*
 * VM functions
 ******************/

/*
 * Generates a new open file entry
 */
void
vm_bootstrap(void)
{
	paddr_t lastpaddr = ram_getsize();
	paddr_t firstpaddr = ram_getfirstfree();

	paddr_t firstpaddr_offset = firstpaddr % PAGE_SIZE;
	coremap_addr = firstpaddr + PAGE_SIZE - firstpaddr_offset;

	coremap = (struct coremap_entry*) PADDR_TO_KVADDR(coremap_addr);

	totalpages = ( (lastpaddr - firstpaddr) / PAGE_SIZE ) + 1; // + 1 to round up
	int coremap_size = totalpages * sizeof(struct coremap_entry);
	int coremap_pages = ( coremap_size / PAGE_SIZE ) + 1; // + 1 to round up

	for (int i = 0; i < coremap_pages; i++) {
		coremap[i].busyFlag = true;
	}

	for (int i = coremap_pages; i < totalpages; i++) {
		coremap[i].busyFlag = false;
	}

	coremap_initialized = true;
}

/* 
 * Fault handling function called by trap code 
 */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	(void) faulttype;
	(void) faultaddress;

	return 0;
}

/* 
 * Allocates npages consecutive kernel pages and marks them in the global coremap if initialized
 */
vaddr_t alloc_kpages(unsigned npages) {

	if (coremap_initialized) {
		// if coremap has been initialized, call the coremap version of allocating memory
		return post_vm_init_alloc(npages);
	} else {
		// if coremap has NOT been initialized, call the stealmem version of allocating memory
		return pre_vm_init_alloc(npages);
	}

	// code should never reach here
	return 0;
}

/*
 * frees the pages in the segment specified by the virtual address, addr
 */
void
free_kpages(vaddr_t addr)
{
	spinlock_acquire(&coremap_lock);

	// iterate through coremap to find page with the virtual address addr
	// if none found, do nothing indicating that page has already been freed/ was never allocated
	for (int i = 0; i < totalpages; i++) {
		if (coremap[i].virtual_addr == addr) {
			// iterate through the next segment_pages of entries in the coremap to free the entire segment
			int segment_pages = coremap[i].segment_pages;
			for (int j = 0; j < segment_pages; j++) {
				coremap[i + j].busyFlag = false;
			}
			break;
		}
	}
	spinlock_release(&coremap_lock);
}


/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void) {}
void vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void) ts;
}
