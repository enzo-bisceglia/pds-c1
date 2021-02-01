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
#include <cpu.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <current.h>
#include <spl.h>
#include <mips/tlb.h>
#include <vfs.h>
#include <uio.h>
#include <vnode.h>
#include "pt.h"
#include "swapfile.h"
#include "vmstats.h"
#include "vm_tlb.h"
#include "coremap.h"

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES    18

int tlb_f, tlb_ff, tlb_fr, tlb_r, tlb_i, pf_z;
int pf_d, pf_e;
/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */
static
void
vm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

vaddr_t
alloc_kpages(unsigned npages){
	
	paddr_t pa;

	vm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr){
	if (vm_is_active()) {
		paddr_t paddr = addr - MIPS_KSEG0;
		freeppages(paddr);
	}
}

void
vm_bootstrap(void){

	if (coremap_bootstrap(ram_getsize())){
		panic("cannot init vm system. Low memory!\n");
	}

	if(pagetable_init(ram_getsize()/PAGE_SIZE)){
		panic("cannot init vm system. Low memory!\n");
  	}
  
  	if(swapfile_init(SWAP_SIZE)){
	  	panic("cannot init vm system. Low memory!\n");
  	}
	
	if (tlb_map_init()){
		panic("cannot init vm system. Low memory!\n");
	}

	tlb_f = tlb_ff = tlb_fr = tlb_r = tlb_i = pf_z = 0;
	pf_d = pf_e = 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	as->count_proc = 0;

	return as;
}

void as_destroy(struct addrspace *as){

  vm_can_sleep();
  pagetable_remove_entries(curproc->pid);
  vfs_close(as->v);
  kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;
	uint32_t ehi,elo;
	pid_t pid = curproc->pid;
	bool full_inv = true;
	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (((ehi & TLBHI_PID) >> 6)==(unsigned int)pid){
			full_inv = false;
			continue;
		}
		else
			tlb_clean_entry(i);//tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	if (full_inv)
		tlb_i++;

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
					off_t offset, int readable, int writeable, int executable)
{
	size_t npages;
	size_t sz2 = sz;

	vm_can_sleep();
	
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
		as->code_offset = offset;
		as->code_sz = sz2;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->data_offset = offset;
		as->data_sz = sz2;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}


int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	
	(void)as;

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

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
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
		as_destroy(proc_getas());
		thread_exit();
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		tlb_f++;
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
	
	int ix = -1;
	
	if(pagetable_getpaddr(faultaddress, &p_temp, pid, &flags)){
		// page hit
		paddr = p_temp;
		tlb_r++;
	}
	else if(swapfile_swapin(faultaddress, &p_temp, pid, as)){
		
		paddr = p_temp; // swapfile hit
		flags = pagetable_getFlagsByIndex(paddr>>12);
		pf_d++;
	}
	else{

		if (faultaddress >= vbase1 && faultaddress < vtop1) {
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
					//indexR contiene indice (in ipt) della pagina da sacrificare
					indexR = pagetable_replacement(pid);
					// ix contiene indice in tlb della pagina da sacrificare
					
					ix = pagetable_getFlagsByIndex(indexR) >> 2;
					swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, pagetable_getPidByIndex(indexR), pagetable_getFlagsByIndex(indexR));
					as->count_proc--;
					paddr = indexR*PAGE_SIZE;
				}
			}
			//-------------------------------------------
			pf_z++;
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

			result = load_page_from_elf(as->v, paddr+(faultaddress==vbase1?as->code_offset&~PAGE_FRAME:0), /* se prima pagina del segmento, scrivo in paddr a partire da offset */
						to_read,
						faultaddress==vbase1?as->code_offset:(as->code_offset&PAGE_FRAME)+faultaddress-vbase1);
						/* se prima pagina del segmento, l'offset sarà code_offset. Altrimenti inizio del segmento più quantitativo che voglio leggere*/
			pf_d++;
			pf_e++;
			
			flags = 0x01; //READONLY
			if (ix!=-1)
				flags |= ix<<2;
			result = pagetable_addentry(faultaddress, paddr, pid, flags);

			
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
			pf_z++;
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
			pf_d++;
			pf_e++;

			if (ix!=-1)
				flags |= ix<<2;
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
			pf_z++;
			if (ix!=-1)
				flags |= ix<<2;
			result = pagetable_addentry(faultaddress, paddr, pid, flags); // qui puoi scrivere
			if(result<0){
				return -1;
			}
			
		}
		else {
			return EFAULT;
		}

	}
	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);
	
	ehi = faultaddress | pid << 6;
	elo = paddr | TLBLO_VALID | TLBLO_GLOBAL;
	if ((flags&0x01)!=0x01) //se non è settato l'ultimo bit allora la pagina è modificabile
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

void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}
