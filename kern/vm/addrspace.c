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
#include <mips/tlb.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include "pt.h"
//#include <coremap.h>
#include "vmstats.h"
#include "opt-paging.h"

//#define PT_LENGTH 1048576
#define SMARTVM_STACKPAGES    18
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */
int tlb_inv = 0;
struct pt_t* ipt;

struct addrspace *
as_create(pid_t pid)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	as->pid = pid;
	/*
	 * Initialize as needed.
	 */

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret, pid_t ret_pid)
{
	struct addrspace *newas;
	int i, j, res;
	paddr_t src, dst;

	newas = as_create(ret_pid);
	if (newas==NULL) {
		return ENOMEM;
	}
#if OPT_SYNCH

	newas->as_npages1 = old->as_npages1;
	newas->as_npages2 = old->as_npages2;
	newas->as_vbase1 = old->as_vbase1;
	newas->as_vbase2 = old->as_vbase2;
	

	res = as_prepare_load(newas);
	if (res){
		return res;
	}
	// O(pt_size*pt_size)
	// acquire ipt_lock
	j=0;
	
	spinlock_acquire(&ipt->ipt_splk);
	for (i=0; i<ipt->pt_size; i++){
		if (ipt->v[i].pid == old->pid){ // find all pages belonging to src address space
			for (; j<ipt->pt_size; j++){ // for each page, find respective one in dst address space
				if (ipt->v[j].pid == ret_pid && ipt->v[j].vaddr == ipt->v[i].vaddr){
					src = i*PAGE_SIZE; // index of dst frame
					dst = j*PAGE_SIZE; // index of src frame
					memcpy((void*)PADDR_TO_KVADDR(dst),
							(const void*)PADDR_TO_KVADDR(src),
							PAGE_SIZE);
					setXbit(ipt->v[j].flags, PRESENT_BIT, 1);
					break;
				}
			}
		}
	}
	spinlock_release(&ipt->ipt_splk);
#endif
	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	int i;

	KASSERT(as->pid>2); // kernel should not be here
	
	spinlock_acquire(&ipt->ipt_splk);
	for (i=0; i<ipt->pt_size; i++){ // clean all curproc-related pages
		if (ipt->v[i].pid == as->pid){
			freeppages(i*PAGE_SIZE);
			ipt->v[i].flags[0] = 0;
		}
	}
	spinlock_release(&ipt->ipt_splk);

	/*struct pte* pt = as->page_table;
	size_t i, en = VADDR_TO_PTEN(as->as_vbase1);
	for (i=0; i<as->as_npages1; i++){
		freeppages(pt[en+i].paddr);
		kfree(pt[en+i].flags); //free bitmap
	}
	
	en = VADDR_TO_PTEN(as->as_vbase2);
	for (i=0; i<as->as_npages2; i++){
		freeppages(pt[en+i].paddr);
		kfree(pt[en+i].flags);
	}

	en = VADDR_TO_PTEN((USERSTACK - (SMARTVM_STACKPAGES*PAGE_SIZE)));
	for (i=0; i<SMARTVM_STACKPAGES; i++){
		freeppages(pt[en+i].paddr);
		kfree(pt[en+i].flags);
	}
	pt_destroy(pt);*/
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
#if OPT_PAGING
		/* invalidate the entries */
		uint32_t ehi, elo;
		tlb_read(&ehi, &elo, i);
		
		if (as->pid == (pid_t)(ehi & TLBHI_PID)>>6) /* entry belongs to current proc ?	*/
			continue;
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
#endif
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	size_t npages;

	//dumbvm_can_sleep();

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	
	int i, j, ix;
	int round[3][2] = {
		{as->as_vbase1, as->as_npages1},
		{as->as_vbase2, as->as_npages2},
		{USERSTACK-SMARTVM_STACKPAGES*PAGE_SIZE, SMARTVM_STACKPAGES}
	};

	//dumbvm_can_sleep();

	spinlock_acquire(&ipt->ipt_splk);
	for (i=0; i<3; i++){
		for (j=0; j<round[i][1]; j++){
			paddr_t paddr = getppages(1);
			if (paddr == 0)
				return ENOMEM;
			ix = paddr >> 12;
			KASSERT(!isSetX(ipt->v[ix].flags, KERNEL_BIT)); // not touching kernel
			ipt->v[ix].pid = as->pid;
			ipt->v[ix].vaddr = round[i][0]+j*PAGE_SIZE;
			as_zero_region(paddr, 1);
		}
	}
	spinlock_release(&ipt->ipt_splk);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

