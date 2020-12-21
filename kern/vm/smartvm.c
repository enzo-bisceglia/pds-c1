#include <types.h> //vaddr_t
#include <kern/errno.h> //EINVAL
#include <lib.h> //panic
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h> //proc
#include <current.h> //curproc
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h> //ram_getsize
#include <pt.h>
#include "vmstats.h"

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;

static unsigned long* alloc_size;
static int tot_frames;
static int vm_active = 0;
int before_vm = 0; //declared in vmstats.h
int should_be_zero = 0; //declared in vmstats.h

struct pt_t* ipt;

static
int
vm_is_active(){
	int active;
	spinlock_acquire(&freemem_lock);
	active = vm_active;
	spinlock_release(&freemem_lock);
	return active;
}

static
void
dumbvm_can_sleep(void){

	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

void
vm_bootstrap(void) {
    /*
	 * Write this.
	 */
	int i;
	paddr_t first;
	struct pte_t* v;
	tot_frames = ram_getsize()/PAGE_SIZE;

	alloc_size = kmalloc(tot_frames*sizeof(unsigned long));
	if (alloc_size == NULL){
		return;
	}
	
	ipt = pt_init(tot_frames);
	if (ipt==NULL)
		panic("smartvm: can't initialize page table.");
	
	/* get first available address */
	first = ram_getfirstfree();
	
	v=ipt->v;
	for (i=0; i<ipt->pt_size; i++){
		if (i<(int)first/PAGE_SIZE)
			setXbit(v[i].flags, KERNEL_BIT, 1); // entry belongs to kernel
		alloc_size[i]=0;
	}
	/* set all previous frames occupied (by kernel) */
	alloc_size[0] = first/PAGE_SIZE;
	
	//never_freed = ((int)alloc_size+(sizeof(unsigned long)*tot_frames)-MIPS_KSEG0)/4096;
	spinlock_acquire(&freemem_lock);
	vm_active = 1;
	spinlock_release(&freemem_lock);

    //coremap_bootstrap();
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {

	/*
	 * Write this.
	 */
	paddr_t paddr;
	int i, ix, res;
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
	
	/*
	 * we're here bc of tlb_miss. (write) Tried to write to a vaddr (can happen ONLY on load or when user does i/o)
	 * (read) tried to read to a vaddr (can happen during normal execution).
	 * - tlb miss & page fault -> page not loaded or swapped out
	 * - tlb miss & page hit -> 2 processes: 1 executing .. cs .. 2 replaces all tlb .. cs again .. 1 has pages in memory but no references in tlb
	 * - tlb hit & page fault -> should not happen
	 * - tlb hit & page hit -> should not happen
	 */

	res = page_walk(faultaddress, curproc->pid, &ix);
	if (res){ // PAGE FAULT
		if (ix==-1){
			panic("page evicted or never allocated.");
			pf_z++;
		}
		setXbit(ipt->v[ix].flags, PRESENT_BIT, 1);
	}
	else
		tlb_rel++;
		
	paddr = ix*PAGE_SIZE;


	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		
		ehi = faultaddress | (as->pid << 6);
		/*  DIRTY = entry can be modified in write
		 *	VALID = entry is a valid mapping (relative process is being executed)
		 *	GLOBAL = 
		 */
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID | TLBLO_GLOBAL;
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


paddr_t
getppages(int np){

	int i, found=-1;
	paddr_t addr;

	if (!vm_is_active()){
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(np);
		before_vm+=np;
		spinlock_release(&stealmem_lock);
		return addr;
	}

	spinlock_acquire(&freemem_lock);
	for (i=0; i<=tot_frames-np;){
		if (alloc_size[i]==0){
			alloc_size[i]=np;
			found = i;
			break;
		}
		i+=alloc_size[i];
	}
	addr = found>=0 ? (paddr_t)found*PAGE_SIZE : 0;
	should_be_zero+=np;
	spinlock_release(&freemem_lock);

	return addr;
	
}


int 
freeppages(paddr_t addr/*, unsigned long npages*/){
  	
	long first;

  	if (!vm_is_active())
  		return 0; 
  	first = addr/PAGE_SIZE;
  	KASSERT(alloc_size!=NULL);
 	KASSERT(tot_frames>first);

  	spinlock_acquire(&freemem_lock);
	should_be_zero-=alloc_size[first];
	alloc_size[first]=0;
  	spinlock_release(&freemem_lock);

  	return 1;
}

vaddr_t
alloc_kpages(unsigned npages) {
    
	paddr_t pa;
	int i, ix;

	dumbvm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}

	// MARK KERNEL
	if (vm_is_active()) {
		ix = pa >> 12; // paddr -> ipt_entry
		spinlock_acquire(&ipt->ipt_splk);
		for (i=0; i<(int)npages; i++)
			setXbit(ipt->v[ix+i].flags, KERNEL_BIT, 1);
		spinlock_release(&ipt->ipt_splk);
	}
	
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr){

	int i, ix, np;
	if (vm_is_active()) {
		paddr_t paddr = addr - MIPS_KSEG0;
		long first = paddr/PAGE_SIZE;
		KASSERT(alloc_size!=NULL);
    	KASSERT(tot_frames>first);
		np = alloc_size[first];
		freeppages(paddr/*, alloc_size[first]*/);
		// UNMARK KERNEL
		ix = paddr >> 12;
		spinlock_acquire(&ipt->ipt_splk);
		for (i=0; i<np; i++)
			setXbit(ipt->v[ix+i].flags, KERNEL_BIT, 0);
		spinlock_release(&ipt->ipt_splk);
	}
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}