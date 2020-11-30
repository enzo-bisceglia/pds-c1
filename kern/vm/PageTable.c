//
// Created by attilio on 10/11/2020.
//

#include "PageTable.h"
#include <lib.h>
#include <vm.h>


static pagetable *pg;

int pagetable_init(int length){
    int i;
    pg = (pagetable *) kmalloc(sizeof(pagetable));
    if(pg==NULL){
        return 0;
    }
    pg->v_pages = kmalloc(sizeof(vaddr_t)*length);
    if(pg->v_pages==NULL) return 0;

    pg->pids = kmalloc(sizeof(pid_t)*length);
    if(pg->pids==NULL) return 0;

    pg -> control = kmalloc(sizeof(uint16_t)*length);
    if(pg ->control==NULL) return 0;


    for(i=0;i<length;i++){
        pg ->control[i] = 0;
	pg->pids[i] = -1;
	pg->v_pages[i] = 0x0;
    }
    pg -> pbase = ram_getfirst() & PAGE_FRAME;
    pg -> length = ((int)ram_getsize())/PAGE_SIZE;
    spinlock_init(&pg->pagetable_lock);
    return 1;
}

int pagetable_addentry(vaddr_t vaddr,paddr_t paddr,pid_t pid,uint16_t flag){
    if (vaddr>MIPS_KSEG0) return -1;
    vaddr_t relative_vaddr = vaddr & PAGE_FRAME;
    paddr &= PAGE_FRAME;
    paddr = paddr - pg->pbase;
    unsigned int frame_index = (int) paddr/PAGE_SIZE;
    KASSERT(frame_index < pg->length);
    spinlock_acquire(&pg->pagetable_lock);
    pg -> v_pages[frame_index] = relative_vaddr;
    pg ->control[frame_index] =flag;
    pg->pids[frame_index] = pid;
    spinlock_release(&pg->pagetable_lock);
    return 1;
}

int pagetable_getpaddr(vaddr_t vaddr, paddr_t *paddr,pid_t *pid,uint16_t *flag){
    unsigned int i;
    vaddr_t relative_vaddr = vaddr & PAGE_FRAME;
    spinlock_acquire(&pg->pagetable_lock);
    paddr_t p =-1;
    for(i=0;i<pg->length;i++){
	if(pg->v_pages[i]==relative_vaddr && pg -> pids[i]==*pid){
	     p = (paddr_t) (i * PAGE_SIZE) + pg->pbase;	
	     break;
	}
    }
    if((int) p == -1) {
    	spinlock_release(&pg->pagetable_lock);
	return 0;
	}
    *paddr = p;
    *pid = pg->pids[i];
    *flag = pg->control[i];
    spinlock_release(&pg->pagetable_lock);
    (void) paddr;
    return 1;
}

void pagetable_remove_entries(pid_t pid){
	KASSERT(pid>=0);
	unsigned int i;
	spinlock_acquire(&pg->pagetable_lock);
	for(i=0;i<pg->length;i++){
		if(pg->pids[i]==pid){
			pg ->control[i] =0;
			pg->pids[i] = -1;
			pg->v_pages[i] = 0x0;	
		}	
	}
	spinlock_release(&pg->pagetable_lock);
}

int pagetable_change_flag(paddr_t paddr,uint16_t flag){
    paddr &= PAGE_FRAME;
    paddr = paddr - pg->pbase;
    unsigned int frame_index = (int) paddr/PAGE_SIZE;
    if(frame_index > pg->length) return 0;
    spinlock_acquire(&pg->pagetable_lock);
    pg ->control[frame_index] =flag;
    spinlock_release(&pg->pagetable_lock);
    return 1;
    
}

void pagetable_destroy(void){
    spinlock_acquire(&pg->pagetable_lock);
    kfree(pg -> v_pages);
    kfree(pg -> control);
    kfree(pg -> pids);
    spinlock_release(&pg->pagetable_lock);
    kfree(pg);
}


