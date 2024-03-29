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
  if (freeRamFrames==NULL) return;  
  allocSize     = kmalloc(sizeof(unsigned long)*nRamFrames);
  if (allocSize==NULL) {    
    /* reset to disable this vm management */
    freeRamFrames = NULL; return;
  }
  for (i=0; i<nRamFrames; i++) {    
    freeRamFrames[i] = (unsigned char)0;
    allocSize[i]     = 0;  
  }

  spinlock_acquire(&freemem_lock);
  allocTableActive = 1;
  spinlock_release(&freemem_lock);

  if(!pagetable_init(((int)ram_getsize())/PAGE_SIZE)){
	panic("Page table allocation fails\n");
  }


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
my_load_segment(struct addrspace *as, struct vnode *v,
                off_t offset, vaddr_t vaddr,
                size_t memsize, size_t filesize,
                int is_executable)
{
    struct iovec iov;
    struct uio u;
    int result;

    if (filesize > memsize) {
        kprintf("ELF: warning: segment filesize > segment memsize\n");
        filesize = memsize;
    }

    DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx\n",
          (unsigned long) filesize, (unsigned long) vaddr);

    iov.iov_ubase = (userptr_t)vaddr;
    iov.iov_len = memsize;		 // length of the memory space
    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = filesize;          // amount to read from the file
    u.uio_offset = offset;
    u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = as;

    result = VOP_READ(v, &u);
    if (result) {
        return result;
    }

    if (u.uio_resid != 0) {

        kprintf("ELF: short read on segment - file truncated?\n");
        return ENOEXEC;
    }

#if 0
    {
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n",
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif

    return result;
}


#define TO_TLB_FLAG(p) (p &= (TLBLO_VALID | TLBLO_DIRTY))

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	//vaddr_t original_faultaddress = faultaddress;
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
	vtop1 = vbase1 + (as->as_npages1 +1)* PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + (as->as_npages2+1) * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK; 

	paddr_t p_temp;
	pid_t pid = curproc -> pid;
	uint16_t flag=0x0;
	flag = flag | TLBLO_VALID | TLBLO_DIRTY;
	(void) p_temp;

	int result = pagetable_getpaddr(faultaddress,&p_temp,&pid,&flag); // tenta di trovare l'indirizzo in pagetable in p_temp passato per riferimento
	if(result==1){ //trovato! l'inserisco in TLB
		paddr = p_temp;	

	}
	else if(result<0) // l'indirizzo passato non era nel range valido della pagetable
		 return EFAULT;

	else if (faultaddress >= vbase1 && faultaddress < vtop1) { // se vero siamo in segmento di codice
	paddr = getppages(1);
	paddr &= PAGE_FRAME;
	as_zero_region(paddr, 1);
	
	i = faultaddress - vbase1 ;

	vaddr_t temp = as->ph1.p_vaddr + i;
	result = pagetable_addentry(temp,paddr,pid,flag);
	if(result<=0) return EFAULT;

	if(faulttype == VM_FAULT_READ){

			ssize_t to_read = PAGE_SIZE;

			if((unsigned int) (i + PAGE_SIZE) > as->ph1.p_filesz) {
				to_read = as->ph1.p_filesz - i;	
			}

//(unsigned int) (i + PAGE_SIZE)>as->ph1.p_filesz ? as->ph1.p_filesz - i : PAGE_SIZE
			result = my_load_segment(as, as->v, i + as -> ph1.p_offset , temp ,
				              PAGE_SIZE,to_read,
				              as->ph1.p_flags & PF_X);
			if(result<0){
				return -1;
				}
			if(to_read != PAGE_SIZE){
				flag=0x0;
				pagetable_change_flag(paddr,flag | TLBLO_VALID);
			}
			return 0;

		}
		//paddr = (faultaddress - vbase1) + as->as_pbase1;

	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = getppages(1);
		paddr &= PAGE_FRAME;
		as_zero_region(paddr, 1);

		
		int i = faultaddress - vbase2 ;
		vaddr_t temp = as->ph2.p_vaddr + i;
		result = pagetable_addentry(temp,paddr,pid, flag);
		if(result<=0) return EFAULT;
		//if(faulttype == VM_FAULT_READ){
		
			result = my_load_segment(as, as->v, i + as -> ph2.p_offset , temp ,
				              PAGE_SIZE,(unsigned int) (i + PAGE_SIZE) > as->ph2.p_filesz ? as->ph2.p_filesz - i : PAGE_SIZE,
				              as->ph2.p_flags & PF_X);


			if(result<0){
				return -1;
				}
			return 0;
			//}

		//paddr = (faultaddress - vbase2) + as->as_pbase2;
		}


	else if (faultaddress >= stackbase && faultaddress < stacktop) {//se falso tutto quello di prima sono in stack
		paddr = getppages(1);
		paddr &= PAGE_FRAME;
		as_zero_region(paddr, 1);
		flag |= (TLBLO_VALID | TLBLO_DIRTY) >> 9;
		result = pagetable_addentry(faultaddress,paddr,pid,flag); // qui puoi scrivere
		//paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */

	KASSERT((paddr & PAGE_FRAME) == paddr);
	//KASSERT((flag & (TLBLO_VALID | TLBLO_DIRTY) >> 9)==flag);
	KASSERT((pid & 0xfff) == pid); // non possono esserci PID >= 2^12;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	//flag &=  TLBLO_VALID | TLBLO_DIRTY;
	
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}

		ehi = faultaddress | pid;

		elo = paddr | flag;

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}






#endif
