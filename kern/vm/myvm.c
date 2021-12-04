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
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES   18
#define PAGE_SIZE    		4096


void
vm_bootstrap(void)
{
	paddr_t lastpaddr = ram_getsize();
	paddr_t firstpaddr = ram_getfirstfree();

	paddr_t firstpaddr_offset = firstpaddr % PAGE_SIZE;
	paddr_t firstpaddr_aligned = firstpaddr + PAGE_SIZE - firstpaddr_offset;

	coremap = (struct coremap_entry*) PADDR_TO_KVADDR(firstpaddr_aligned);

	totalpages = ( (lastpaddr - firstpaddr) / PAGE_SIZE ) + 1; // + 1 to round up
	int coremap_size = totalpages * sizeof(struct coremap_entry);
	int coremap_pages = ( coremap_size / PAGE_SIZE ) + 1; // + 1 to round up

	for (int i = 0; i < coremap_pages; i++) {
		coremap[i].busyFlag = true;
		coremap[i].address = firstpaddr_aligned + (i * PAGE_SIZE);
	}

	for (int i = coremap_pages; i < totalpages; i++) {
		coremap[i].busyFlag = false;
		coremap[i].address = firstpaddr_aligned + (i * PAGE_SIZE);
	}
}


/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	(void) faulttype;
	(void) faultaddress;

	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages) {
	paddr_t start_addr;

	spinlock_acquire(&coremap_lock);

	int alloced = 0;
	int start = -1;

	for (int i = 0; i < totalpages || alloced == (int) npages; i++) {
		if (!coremap[i].busyFlag && start == -1) {
			alloced++;
			start = i;
		} else if (!coremap[i].busyFlag) {
			alloced++;
		} else {
			alloced = 0;
			start = -1;
		}
	}

	if (alloced < (int) npages) {
		spinlock_release(&coremap_lock);
		return 0;
	}

	start_addr = coremap[start].address;
	as_zero_region(start_addr, npages);

	vaddr_t ret = PADDR_TO_KVADDR(start_addr);
	coremap[start].numpages = npages;
	coremap[start].virtual_addr = ret;

	for (int i = 0; i < (int) npages; i++) {
		coremap[i + start].busyFlag = true;
	}

	spinlock_release(&coremap_lock);
	return ret;
}

void
free_kpages(vaddr_t addr)
{
	spinlock_acquire(&coremap_lock);
	for (int i = 0; i < totalpages; i++) {
		if (coremap[i].virtual_addr == addr) {
			int numpages = coremap[i].numpages;
			for (int j = 0; j < numpages; j++) {
				coremap[i + j].busyFlag = false;
			}
			break;
		}
	}
	spinlock_release(&coremap_lock);
}


void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void) {

}
void vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void) ts;
}
