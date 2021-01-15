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
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>

#include <vm.h>
#include <uio.h>
#include <vnode.h>
#include <elf.h>
#include "PageTable.h"
#include "swapfile.h"
#include "vmstats.h"
#include "vm_tlb.h"

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
#define DUMBVM_STACKPAGES    36

/* G.Cabodi: set DUMBVM_WITH_FREE
 *  - 0: original dumbvm
 *  - 1: support for alloc/free
 */
#define DUMBVM_WITH_FREE 1

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if DUMBVM_WITH_FREE

/* G.Cabodi - support for free/alloc */

static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;


static unsigned char *freeRamFrames = NULL;
static unsigned long *allocSize = NULL;
static int nRamFrames = 0;
int leakage;

static int allocTableActive = 0;

static int isTableActive () {
  int active;
  spinlock_acquire(&freemem_lock);
  active = allocTableActive;
  spinlock_release(&freemem_lock);
  return active;
}

void
vm_bootstrap(void)
{
  	int i;

	nRamFrames = ((int)ram_getsize())/PAGE_SIZE;

  	/* alloc freeRamFrame and allocSize */  
 	freeRamFrames = kmalloc(sizeof(unsigned char)*nRamFrames);
  	if (freeRamFrames==NULL)
	  	return;
	
  	allocSize = kmalloc(sizeof(unsigned long)*nRamFrames);
  	if (allocSize==NULL) {    
    /* reset to disable this vm management */
    	freeRamFrames = NULL;
		return;
  	}

  	for (i=0; i<nRamFrames; i++) {    
    	freeRamFrames[i] = (unsigned char)0;
    	allocSize[i] = 0;  
  	}

  	spinlock_acquire(&freemem_lock);
  	allocTableActive = 1;
  	spinlock_release(&freemem_lock);

  	if(pagetable_init(nRamFrames)){
		panic("Page table allocation failed.\n");
  	}
  
  	if(swapfile_init(SWAP_SIZE)){
	  	panic("Swap table allocation failed\n");
  	}
	
	if (tlb_map_init()){
		panic("tlb map allocation failed\n");
	}

	leakage = 0;
}

/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */
void
dumbvm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

static paddr_t 
getfreeppages(unsigned long npages) {
  paddr_t addr;	
  long i, first, found, np = (long)npages;

  if (!isTableActive()) return 0; 
  spinlock_acquire(&freemem_lock);
  for (i=0,first=found=-1; i<nRamFrames; i++) {
    if (freeRamFrames[i]) {
      if (i==0 || !freeRamFrames[i-1]) 
        first = i; /* set first free in an interval */   
      if (i-first+1 >= np) {
        found = first;
        break;
      }
    }
  }
	
  if (found>=0) {
    for (i=found; i<found+np; i++) {
      freeRamFrames[i] = (unsigned char)0;
    }
    allocSize[found] = np;
    addr = (paddr_t) found*PAGE_SIZE;
  }
  else {
    addr = 0;
  }

  spinlock_release(&freemem_lock);

  return addr;
}

paddr_t
getppages(unsigned long npages)
{
  paddr_t addr;

  /* try freed pages first */
  addr = getfreeppages(npages);
  if (addr == 0) {
    /* call stealmem */
    spinlock_acquire(&stealmem_lock);
    addr = ram_stealmem(npages);
    spinlock_release(&stealmem_lock);
  }
  if (addr!=0 && isTableActive()) {
    spinlock_acquire(&freemem_lock);
    allocSize[addr/PAGE_SIZE] = npages;
	leakage += npages;
    spinlock_release(&freemem_lock);
  } 

  return addr;
}

int 
freeppages(paddr_t addr, unsigned long npages){
  long i, first, np=(long)npages;	

  if (!isTableActive()) return 0; 
  first = addr/PAGE_SIZE;
  KASSERT(allocSize!=NULL);
  KASSERT(nRamFrames>first);

  spinlock_acquire(&freemem_lock);
  for (i=first; i<first+np; i++) {
    freeRamFrames[i] = (unsigned char)1;
  }
  leakage -= npages;
  spinlock_release(&freemem_lock);

  return 1;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	dumbvm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr){
  if (isTableActive()) {
    paddr_t paddr = addr - MIPS_KSEG0;
    long first = paddr/PAGE_SIZE;	
    KASSERT(allocSize!=NULL);
    KASSERT(nRamFrames>first);
    freeppages(paddr, allocSize[first]);	
  }
}


void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

static
int
load_page_from_elf(struct vnode* v, paddr_t dest, size_t len, off_t offset){
	
	struct iovec iov;
	struct uio ku;
	int res;

	uio_kinit(&iov, &ku, (void*)PADDR_TO_KVADDR(dest), len, offset, UIO_READ);
	res = VOP_READ(v, &ku);
	if (res){
		return res;
	}

	if (ku.uio_resid!=0){
		return ENOEXEC;
	}

	return res;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	//int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	//int spl;
	int indexR;
	size_t to_read;

	int result;

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

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	//KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	//KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	//KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	//KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	//KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + (as->as_npages1)* PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + (as->as_npages2) * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	paddr_t p_temp;
	pid_t pid = curproc -> pid;
	unsigned char flags = 0;
	(void) p_temp;
	int ix = -1;
	
	if(pagetable_getpaddr(faultaddress, &p_temp, pid, &flags)){
		// page hit
		paddr = p_temp;
	}
	else if(swapfile_swapin(faultaddress, &p_temp, pid, as)){
		paddr = p_temp; // swapfile hit
	}
	else if (faultaddress >= vbase1 && faultaddress < vtop1) {
		//Domenico -- GESTIONE PAGE REPLACEMENT
		as->count_proc++;
		if (as->count_proc>=MAX_PROC_PT){
			indexR = pagetable_replacement(pid); //find page to be replaced
			ix = pagetable_getFlagsByIndex(indexR) >> 2; //retrieve tlb index of page being replaced (it will be used to clean the tlb entry)
			swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
			as->count_proc--;
			paddr = indexR*PAGE_SIZE;
		}
		else{
			paddr = getppages(1);
			if (paddr==0){
				indexR = pagetable_replacement(pid);
				ix = pagetable_getFlagsByIndex(indexR) >> 2;
				swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
				as->count_proc--;
				paddr = indexR*PAGE_SIZE;
			}
		}
		//-------------------------------------------
		as_zero_region(paddr, 1);
		/*
		 * l'errore col caricamento precedente stava nel fatto che a prescindere dall' "offset virtuale"
		 * in cui si trovasse un segmento del programma, questo veniva comunque caricato all' inizio della
		 * corrispondente pagina fisica. Il risultato era ovviamente un esecuzione "disallineata". 
		*/
		 
		if (faultaddress == vbase1){ // mi trovo all' inizio della prima pagina (il segmento si trova all'inizio o è presente un offset?)
			if (as->code_sz<PAGE_SIZE-(as->code_offset&~PAGE_FRAME)) // la quantità da leggere dipende dalla dimensione del segmento (è < 4KB ?)
				to_read = as->code_sz;
			else
				to_read = PAGE_SIZE-(as->code_offset&~PAGE_FRAME);
		}
		else if (faultaddress == vtop1 - PAGE_SIZE){ // mi trovo all' inizio dell' ultima pagina
			to_read = as->code_sz - (as->as_npages1-1)*PAGE_SIZE; // quanti bytes ci sono dall' inizio del segmento (virtuale)?
			if (as->code_offset&~PAGE_FRAME) // rimuovo eventuale offset della prima pagina
				to_read-=(as->code_offset&~PAGE_FRAME);
		}
		else //mi trovo in una pagina intermedia del segmento (sicuramente 4KB consecutivi)
			to_read = PAGE_SIZE;

		result = load_page_from_elf(as->v, paddr+(faultaddress==vbase1?as->code_offset&~PAGE_FRAME:0), /* dove (indirizzo fisico) andare a scrivere */
					to_read,
					faultaddress==vbase1?as->code_offset:(as->code_offset&PAGE_FRAME)+faultaddress-vbase1/* offset (nel file) da cui leggere */);
		flags = 0x01; //READONLY
		result = pagetable_addentry(faultaddress, paddr, pid, flags);

		// ?
		if(result<0){
			return -1;
		}
		
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		//Domenico -- GESTIONE PAGE REPLACEMENT
		as->count_proc++;
		if (as->count_proc>=MAX_PROC_PT){
			indexR = pagetable_replacement(pid);
			ix = pagetable_getFlagsByIndex(indexR) >> 2; //overwrite tlb_index
			swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
			as->count_proc--;
			paddr = indexR*PAGE_SIZE;
		}
		else{
			paddr = getppages(1);
			if (paddr==0){
				indexR = pagetable_replacement(pid);
				ix = pagetable_getFlagsByIndex(indexR) >> 2; //overwrite tlb_index
				swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
				as->count_proc--;
				paddr = indexR*PAGE_SIZE;
			}
		}
		//-------------------------------------------
		as_zero_region(paddr, 1);

		if (faultaddress == vbase2){
			if (as->data_sz<PAGE_SIZE-(as->data_offset&~PAGE_FRAME))
				to_read = as->data_sz;
			else
				to_read = PAGE_SIZE-(as->data_offset&~PAGE_FRAME);
		}
		else if (faultaddress == vtop2 - PAGE_SIZE){
			to_read = as->data_sz - (as->as_npages2-1)*PAGE_SIZE;
			if (as->data_offset&~PAGE_FRAME)
				to_read-=(as->data_offset&~PAGE_FRAME);
		}
		else
			to_read = PAGE_SIZE;
		

		result = load_page_from_elf(as->v, paddr+(faultaddress==vbase2?as->data_offset&~PAGE_FRAME:0),
						to_read, 
						faultaddress==vbase2?as->data_offset:(as->data_offset&PAGE_FRAME)+faultaddress-vbase2);
	
		result = pagetable_addentry(faultaddress, paddr, pid, flags);
		
		// ?
		if(result<0){
			return -1;
		}
		
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {//se falso tutto quello di prima sono in stack
		//Domenico -- GESTIONE PAGE REPLACEMENT
		as->count_proc++;
		if (as->count_proc>=MAX_PROC_PT){
			indexR = pagetable_replacement(pid);
			ix = pagetable_getFlagsByIndex(indexR) >> 2; //overwrite tlb_index
			swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
			as->count_proc--;
			paddr = indexR*PAGE_SIZE;
		}
		else{
			paddr = getppages(1);
			if (paddr==0){
				indexR = pagetable_replacement(pid);
				ix = pagetable_getFlagsByIndex(indexR) >> 2; //overwrite tlb_index
				swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
				as->count_proc--;
				paddr = indexR*PAGE_SIZE;
			}
		}
		//-------------------------------------------

		as_zero_region(paddr, 1);
		result = pagetable_addentry(faultaddress, paddr, pid, flags); // qui puoi scrivere
		if(result<0){
			return -1;
		}
		
	}
	else {
		return EFAULT;
	}

	
	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);
	
	ehi = faultaddress | pid << 6;
	elo = paddr | TLBLO_VALID | TLBLO_GLOBAL;
	if ((flags&0x01)!=0x01)
		elo |= TLBLO_DIRTY; //page is modifiable
	
	/* Abbiamo inserito l'informazione sul pid perciò TLBLO_GLOBAL non dovrebbe essere presente. Tuttavia
	  per motivi a me oscuri (probabilmente va modificato qualche registro della cpu in modo tale che
	  la cpu possa associare il pid del processo in esecuzione e con un pid trovato nelle entry **tale registro è entryhi**)
	  se non setto il flag, il sistema va in crash (questo si può spiegare).
	  Ad ogni modo, è utile avere il pid a portata così da evitare il flush ad ogni context switch ma
	  implementare le system call per sincronizzazione non ha senso, i programmi che si basano e.g. su 
	  fork sarebbero inutilizzabili (con TLBLO_GLOBAL e due processi che fanno riferimento allo stesso vaddr generano
	  errore di entry duplicata in tlb)
	*/
	tlb_write_entry(&ix, ehi, elo);
	KASSERT(ix!=-1);
	pagetable_setFlagsAtIndex(paddr>>12, ix<<2);
	return 0;
	
	/*****************/
	/* Disable interrupts on this CPU while frobbing the TLB. */
	/*spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) { // new vaddr in old paddr
			continue;
		}

		ehi = faultaddress | pid << 6;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID | TLBLO_GLOBAL;

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;*/
}
#endif
