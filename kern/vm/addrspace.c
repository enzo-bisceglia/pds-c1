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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <coremap.h>
#include "vmstats.h"

#define SMARTVM_STACKPAGES    18
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */
int tlb_inv = 0;

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->page_table = pt_init();
	if (as->page_table == NULL) {
		panic("smartvm: can't initialize page table.");
	}
	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	/*
	 * Initialize as needed.
	 */

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}
#if OPT_SYNCH
	size_t i;
	int result, en;

	newas->as_npages1 = old->as_npages1;
	newas->as_npages2 = old->as_npages2;
	newas->as_vbase1 = old->as_vbase1;
	newas->as_vbase2 = old->as_vbase2;

	result = as_prepare_load(newas);
	if (result){
		return result;
	}
	// copy code
	en = VADDR_TO_PTEN(newas->as_vbase1);
	for (i=0; i<newas->as_npages1; i++){
		memcpy((void*)PADDR_TO_KVADDR(newas->page_table[en+i].paddr),
			(const void*)PADDR_TO_KVADDR(old->page_table[en+i].paddr),
			PAGE_SIZE);
	}
	// copy data
	en = VADDR_TO_PTEN(newas->as_vbase1);
	for (i=0; i<newas->as_npages1; i++){
		memcpy((void*)PADDR_TO_KVADDR(newas->page_table[en+i].paddr),
			(const void*)PADDR_TO_KVADDR(old->page_table[en+i].paddr),
			PAGE_SIZE);
	}
	// copy stack
	en = VADDR_TO_PTEN((USERSTACK-(SMARTVM_STACKPAGES*PAGE_SIZE)));
	for (i=0; i<SMARTVM_STACKPAGES; i++){
		memcpy((void*)PADDR_TO_KVADDR(newas->page_table[en+i].paddr),
			(const void*)PADDR_TO_KVADDR(old->page_table[en+i].paddr),
			PAGE_SIZE);
	}
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
	struct pte* pt = as->page_table;
	size_t i, en = VADDR_TO_PTEN(as->as_vbase1);
	for (i=0; i<as->as_npages1; i++){
		freeppages(pt[en+i].paddr);
	}
	
	en = VADDR_TO_PTEN(as->as_vbase2);
	for (i=0; i<as->as_npages2; i++){
		freeppages(pt[en+i].paddr);
	}

	en = VADDR_TO_PTEN((USERSTACK - (SMARTVM_STACKPAGES*PAGE_SIZE)));
	for (i=0; i<SMARTVM_STACKPAGES; i++){
		freeppages(pt[en+i].paddr);
	}
	pt_destroy(pt);
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
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	tlb_inv++;

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
	size_t i;
	struct pte* pt = as->page_table;

	//dumbvm_can_sleep();
	size_t en = VADDR_TO_PTEN(as->as_vbase1);
	KASSERT(en<PT_LENGTH);
	for (i=0; i<as->as_npages1; i++){
		pt[en+i].paddr = getppages(1);
		if (pt[en+i].paddr == 0)
			return ENOMEM;
		as_zero_region(pt[en+i].paddr, 1);
		//pt[en+i].valid = 1;
	}

	en = VADDR_TO_PTEN(as->as_vbase2);
	KASSERT(en<PT_LENGTH);
	for (i=0; i<as->as_npages2; i++){
		pt[en+i].paddr = getppages(1);
		if (pt[en+i].paddr == 0)
			return ENOMEM;
		as_zero_region(pt[en+i].paddr, 1);
		//pt[en+i].valid = 1;
	}
	
	en = VADDR_TO_PTEN((USERSTACK-(SMARTVM_STACKPAGES*PAGE_SIZE)));
	KASSERT(en<PT_LENGTH);
	for (i=0; i<SMARTVM_STACKPAGES; i++){
		pt[en+i].paddr = getppages(1);
		if (pt[en+i].paddr == 0)
			return ENOMEM;
		as_zero_region(pt[en+i].paddr, 1);
		//pt[en+i].valid = 1;
	}

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

