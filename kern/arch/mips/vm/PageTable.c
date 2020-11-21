//
// Created by attilio on 10/11/2020.
//

#include "PageTable.h"
#include "stdlib.h"
#include "Allocator.h"



pagetable *pagetable_init(int length, vaddr_t vaddr_base){
    int i;
    pagetable *pg = (pagetable *) kmalloc(sizeof(struct page_table));
    if(pg==NULL){
        return NULL;
    }

    pg->length = length;
    pg->p_frames = kmalloc(sizeof(paddr_t)*length/PAGE_SIZE);
    if(pg->p_frames==NULL) return NULL;
    pg -> control = kmalloc(sizeof(char)*length/PAGE_SIZE);
    if(pg ->p_frames==NULL) return NULL;
    pg ->base_vaddr=vaddr_base;
    for(i=0;i<length;i++){
        pg->p_frames[i] = -1;
        pg ->control[i] =-1;
    }
    pg -> pagetable_lock=SPINLOCK_INITIALIZER;

    return pg;
}

int pagetable_addentry(pagetable *pg,vaddr_t vaddr,paddr_t paddr,char flag){
    if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    int page_index = (int)relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return -2;
    spinclock_acquire(pg->pagetable_lock);
    pg ->p_frames[page_index] = paddr;
    pg->control[page_index] = flag;
    spinclock_release(pg->pagetable_lock);
    return 1;
}

int pagetable_removeaddr(pagetable *pg,vaddr_t vaddr){
    if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    int page_index =(int) relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return -2;
    spinlock_acquire(pg->pagetable_lock);

    pg ->p_frames[page_index] = -1;
    pg->control[page_index] = -1;
    spinclock_release(pg->pagetable_lock);

    return 1;
}

int pagetable_getpaddr(pagetable *pg,vaddr_t vaddr){
    if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    int page_index = (int)relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return -2;
    spinlock_acquire(pg->pagetable_lock);

    return pg ->p_frames[page_index];
    spinlock_release(pg->pagetable_lock);

}

char pagetable_getcontrolbit(pagetable *pg, vaddr_t vaddr){
    if (vaddr<pg->base_vaddr) return -1;
    vaddr_t relative_vaddr = vaddr - pg->base_vaddr;
    int page_index = (int) relative_vaddr/PAGE_SIZE;
    if(page_index>=pg->length) return -2;
    spinlock_acquire(pg->pagetable_lock);

    return pg ->control[page_index];
    spinlock_release(pg->pagetable_lock);

}

int pagetable_getlength(pagetable *pg){
    spinlock_acquire(pg->pagetable_lock);
    return  pg->length;
    spinclock_release(pg->pagetable_lock);

}

void  pagetable_destroy(pagetable *pg){
    my_free(pg ->p_frames);
    my_free(pg -> control);
    my_free(pg);
}

int pagetable_fill(pagetable *pg){
    int i;
    for(i=0;i<pg->length;i++){
        pg->p_frames[i] =
    }
}

