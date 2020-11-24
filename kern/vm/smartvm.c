#include <types.h> //vaddr_t
#include <kern/errno.h> //EINVAL
#include <lib.h> //panic
#include <spl.h>
#include <proc.h> //proc
#include <current.h> //curproc
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h> //ram_getsize

#include <coremap.h>
#include "vmstats.h"

int tlb_faults;
int tlb_faults_free;
int tlb_faults_repl;


void
vm_bootstrap(void) {
    /*
	 * Write this.
	 */
	tlb_faults = 0;
	tlb_faults_free = 0;
	tlb_faults_repl = 0;
    coremap_bootstrap();
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {

	/*
	 * Write this.
	 */
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		tlb_faults++;
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - 18/*SMARTVM_STACKPAGES*/ * PAGE_SIZE;
	stacktop = USERSTACK;

	if ((faultaddress >= vbase1 && faultaddress < vtop1)
	|| (faultaddress >= vbase2 && faultaddress < vtop2)
	|| (faultaddress >= stackbase && faultaddress < stacktop)){
		struct pte* pt = as->page_table;
		size_t en = VADDR_TO_PTEN(faultaddress);
		KASSERT(pt[en].valid==1);
		paddr = pt[en].paddr;	
	}
	else{
		return EFAULT;
	}
	
	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "smartvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		tlb_faults_free++;
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	tlb_faults_repl++;
	splx(spl);
	return EFAULT;
}

vaddr_t
alloc_kpages(unsigned npages) {
    
	paddr_t pa;

	//dumbvm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr){

	paddr_t pa = addr - MIPS_KSEG0;
	freeppages(pa);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}