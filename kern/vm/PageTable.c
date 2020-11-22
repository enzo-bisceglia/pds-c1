//
// Created by attilio on 10/11/2020.
//

#include "PageTable.h"
#include <lib.h>
#include <vm.h>



pagetable *pagetable_init(int length, vaddr_t vaddr_base){
    int i;
    pagetable *pg = (pagetable *) kmalloc(sizeof(pagetable));
    if(pg==NULL){
        return NULL;
    }

    pg->length = length;
    pg->p_frames = kmalloc(sizeof(paddr_t)*length);
    if(pg->p_frames==NULL) return NULL;
    pg -> control = kmalloc(sizeof(char)*length);
    if(pg ->p_frames==NULL) return NULL;
    pg ->base_vaddr=vaddr_base;
    for(i=0;i<length;i++){
        pg->p_frames[i] = -1;
        pg ->control[i] =-1;
    }

    return pg;
}

int pagetable_addentry(pagetable *pg,vaddr_t vaddr,paddr_t paddr,char flag){
    if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    unsigned int page_index = (int)relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return 0;
    spinlock_acquire(&pg->pagetable_lock);
    pg ->p_frames[page_index] = paddr;
    pg->control[page_index] = flag;
    spinlock_release(&pg->pagetable_lock);
    return 1;
}

int pagetable_getpaddr(pagetable *pg,vaddr_t vaddr, paddr_t *paddr){

if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    unsigned int page_index = (int)relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return -2;
    spinlock_acquire(&pg->pagetable_lock);
    paddr_t p = pg->p_frames[page_index];
    spinlock_release(&pg->pagetable_lock);
    if((int) p == -1) return 0;
    *paddr = p;
    (void) paddr;
    return 1;
}

void pagetable_destroy(pagetable *pg){
    kfree(pg->p_frames);
    kfree(pg -> control);
    kfree(pg);
}


