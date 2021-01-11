//
// Created by attilio on 10/11/2020.
//

#include "PageTable.h"
#include <lib.h>
#include <vm.h>


static struct pt_t *ipt;

int
pagetable_init(unsigned int length){

    unsigned int i;

    //allocate inverted page table
    ipt = kmalloc(sizeof(struct pt_t));
    if (ipt == NULL)
        return 1;
    
    //allocate entries in struct
    ipt->v = kmalloc(length*sizeof(struct pte_t));
    if (ipt->v == NULL){
        ipt = NULL;
        return 1;
    }

    for (i=0; i<length; i++){
        ipt->v[i].vaddr = 0;
        ipt->v[i].old_count = 0;
        ipt->v[i].pid = -1;
        ipt->v[i].flags = 0;
    }

    ipt->length = (unsigned int)length;
    spinlock_init(&ipt->pt_lock);

    return 0;
}

int pagetable_addentry(vaddr_t vaddr, paddr_t paddr, pid_t pid, unsigned char flags){
    
    unsigned int i;
    unsigned int frame_index = (int) paddr >> 12;
    struct pte_t *pte;

    KASSERT(frame_index < ipt->length);

    spinlock_acquire(&ipt->pt_lock);
    pte = &ipt->v[frame_index];
    pte->vaddr = vaddr;
    pte->pid = pid;
    pte->flags = flags;
    pte->old_count = 0;

    //se riesco ad aggiungere una entry alla pagetable
    //allora devo incrementare il contatore old_count
    //per tutti i processi  con lo stesso PID
    for (i=0; i<ipt->length; i++){
        pte = &ipt->v[i];
        if(pte->pid==pid && i!=frame_index){
            pte->old_count += 1;
        }
    }
    spinlock_release(&ipt->pt_lock);
   
    return 0;
}

int pagetable_replacement(pid_t pid){
    
    unsigned int i;
    struct pte_t pte;
    int max = 0;
    int replace_index;

    // FIFO policy
    for (i=0; i<ipt->length; i++){
        pte = ipt->v[i];
        if (pte.old_count > max && pte.pid==pid){ 
            max = pte.old_count;
            replace_index = i;
        }
    }
    
    return replace_index;
}


int pagetable_getpaddr(vaddr_t vaddr, paddr_t* paddr, pid_t pid, unsigned char* flags){
    
    unsigned int i, j=ipt->length, res;
    struct pte_t* pte;

    spinlock_acquire(&ipt->pt_lock);
    for(i=0; i<ipt->length; i++){
        pte = &ipt->v[i];
        if (pte->pid == pid){
            if (pte->vaddr == vaddr){
                pte->old_count = 0;
                j = i;
            }
            else
                pte->old_count+=1; //increment other pages of same pid
        }
    }
    spinlock_release(&ipt->pt_lock);

    if(j==ipt->length){
    	res = 0;
    }
	else {
       *paddr = j * PAGE_SIZE;
       // pid don't change
       *flags = ipt->v[j].flags;
       res = 1;
    }

    return res;
}

void pagetable_remove_entries(pid_t pid){
	
	unsigned int i;
    struct pte_t *pte;

    KASSERT(pid>=0);

	spinlock_acquire(&ipt->pt_lock);
	for(i=0; i<ipt->length; i++){
        pte = &ipt->v[i];
		if(pte->pid == pid){
			pte->flags = 0;
            pte->pid = -1;
            pte->vaddr = 0;
            pte->old_count = 0;
            freeppages(i*PAGE_SIZE, 1);
		}
        
	}
	spinlock_release(&ipt->pt_lock);
}

void pagetable_remove_entry(int replace_index){
    
    spinlock_acquire(&ipt->pt_lock);
    ipt->v[replace_index].flags = 0;
    ipt->v[replace_index].pid = -1;
    ipt->v[replace_index].vaddr = 0;
    spinlock_release(&ipt->pt_lock);
}

void pagetable_destroy(void){
    unsigned int i;

    spinlock_acquire(&ipt->pt_lock);
    for (i=0; i<ipt->length; i++){
        kfree((void*)&ipt->v[i]);
    }
    spinlock_release(&ipt->pt_lock);

    kfree(ipt);
}

vaddr_t pagetable_getVaddrByIndex(int index){
    return ipt->v[index].vaddr;
}

paddr_t pagetable_getPaddrByIndex(int index){
    return index*PAGE_SIZE;
}

pid_t pagetable_getPidByIndex(int index){
    return ipt->v[index].pid;
}

unsigned char pagetable_getFlagsByIndex(int index){
    return ipt->v[index].flags;
}

void pagetable_setTlbIndex(int index, unsigned char val){
    ipt->v[index].flags &= 0x03; //clean
    ipt->v[index].flags |= (val << 2); //set
}

void pagetable_setFlagsAtIndex(int index, unsigned char val){
    ipt->v[index].flags |= val;
}
