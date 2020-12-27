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
/*
int pagetable_replacement(vaddr_t temp,paddr_t paddr,pid_t pid,uint16_t flag){
    unsigned int i;
    int max = 0;
    //vaddr_t temp_v_pages;
    //pid_t temp_pids;
    //uint16_t temp_control;
    int replace_index;

    //Applico una politica FIFO
    for (i=0; i<pg->length; i++){
        if (pg->old_count[i] > max && pg->pids[i]==pid){
            max = pg->old_count[i];
            //temp_control = pg->control[i];
            //temp_pids = pg->pids[i];
            //temp_v_pages = pg->v_pages[i];
            replace_index = i;
        }
    }
    
    if(max!=0){
        //vuol dire che è stata trovata una pagina da sostituire

        //Qui bisogna richiamare la funzione su swapfile.c che scrive la pagina vittima su file SWAPFILE
        //-------QUI


        //pagetable_remove_entry(temp_pids, temp_control, temp_v_pages);
        pagetable_remove_entry(replace_index);
        pagetable_addentry(temp, paddr, pid, flag);
    }
    return 1;
}
*/
int pagetable_replacement(pid_t pid){
    unsigned int i;
    struct pte_t pte;
    int max = 0;
    int replace_index;

    for (i=0; i<ipt->length; i++){
        pte = ipt->v[i];
        if (pte.old_count > max && pte.pid==pid){
            max = pte.old_count;
            replace_index = i;
        }
    }
    
    return replace_index;
}


int pagetable_getpaddr(vaddr_t vaddr, paddr_t* paddr, pid_t* pid, unsigned char* flags){
    
    unsigned int i, res;
    struct pte_t pte;

    spinlock_acquire(&ipt->pt_lock);
    for(i=0; i<ipt->length; i++){
        pte = ipt->v[i];
        if(pte.vaddr == vaddr && pte.pid == *pid)
            break;
    }
    spinlock_release(&ipt->pt_lock);

    if(i==ipt->length){
    	res = 0;
    }
	else {
       *paddr = i * PAGE_SIZE;
       // pid don't change
       *flags = ipt->v[i].flags;
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
		}	
	}
	spinlock_release(&ipt->pt_lock);
}
/*
void pagetable_remove_entry(pid_t pid, uint16_t control, vaddr_t v_page){
    KASSERT(pid<0);
    unsigned int i;
	spinlock_acquire(&pg->pagetable_lock);
	for(i=0;i<pg->length;i++){
		if(pg->pids[i]==pid && pg->control[i] == control && pg->v_pages[i] == v_page){
			pg ->control[i] =0;
			pg->pids[i] = -1;
			pg->v_pages[i] = 0x0;	
		}	
	}
	spinlock_release(&pg->pagetable_lock);

}
*/

void pagetable_remove_entry(int replace_index){
    
    spinlock_acquire(&ipt->pt_lock);
    ipt->v[replace_index].flags = 0;
    ipt->v[replace_index].pid = -1;
    ipt->v[replace_index].vaddr = 0;
    spinlock_release(&ipt->pt_lock);
}

int pagetable_change_flags(paddr_t paddr, unsigned char flags){
    
    unsigned int frame_index = (int) paddr >> 12;

    KASSERT(frame_index<ipt->length);

    spinlock_acquire(&ipt->pt_lock);
    ipt->v[frame_index].flags = flags;
    spinlock_release(&ipt->pt_lock);

    return 1;
    
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

uint16_t pagetable_getControlByIndex(int index){
    return ipt->v[index].flags;
}