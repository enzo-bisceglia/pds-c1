#include <types.h> //vaddr_t
#include <kern/errno.h> //EINVAL
#include <lib.h> //panic
#include <vm.h> //ram_getsize

#include <coremap.h>

void
vm_bootstrap(void) {
    /*
	 * Write this.
	 */
    coremap_bootstrap();
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {

	/*
	 * Write this.
	 */

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	/*
	 * gestisci fault
	 * 
	 */
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