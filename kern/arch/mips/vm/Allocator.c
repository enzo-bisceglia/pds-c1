//
// Created by attil on 10/11/2020.
//

#include "Allocator.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <stdio.h>



struct bitMap{
     unsigned char *freeRamFrames; //1 se libero 0 se occupato
     int nRamFrames;
     struct spinlock freemem_lock;
     int PAGE_SIZE;
}typedef bitmap; //questa struct dovrebbe essere dichiarata static in dumbvm è usata com bitmap;

static bitmap *bm;

bitmap * bitmap_init(int ram_size,int PAGE_SIZE){
    int i;
    bm = kmalloc(sizeof(struct bitMap));
    bm->freemem_lock=SPINLOCK_INITIALIZER;
    bm->nRamFrames = ((int)ram_size)/PAGE_SIZE;
    bm->PAGE_SIZE=PAGE_SIZE;
    /* alloc freeRamFrame and allocSize */
    bm->freeRamFrames = kmalloc(sizeof(unsigned char)*bm->nRamFrames);
    if (bm->freeRamFrames==NULL) return NULL;
    for (i=0; i<bm->nRamFrames; i++) {
        bm->freeRamFrames[i] = (unsigned char)1;
    }
}



static paddr_t
getppages(unsigned long npages) //cerca pagine continue, non usa la ram_stelmem
{
    int i,j,found,counter;
    paddr_t addr=0;
    spinlock_acquire(bm->freemem_lock);
    for(i=0;i<bm->nRamFrames;i++){
        if(bm->freeRamFrames[i]==1){
            found=i;
            counter=1;
            for(j=i+1;j<bm->nRamFrames;j++){
                if(bm->freeRamFrames[j]==1) {
                    counter++;
                    if(counter==npages) break;
                }
            }
        }
    }
    if(counter==npages){
        for(i=found;i<found+npages;i++) bm->freeRamFrames[i]=0;
        addr = found*bm->PAGE_SIZE;
    }
    spinlock_release(bm->freemem_lock);

    return addr;
}


static int
freepage(int index){ //rilascia una pagina
    if(index>=bm->nRamFrames * bm->PAGE_SIZE) return 0;
    spinlock_acquire(freemem_lock);
    bm->freeRamFrames[index] =1;
    spinlock_release(freemem_lock);

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
free_kpages(vaddr_t addr,int npages){ //rilascia npages pagine contigue
    int i;
    paddr_t paddr = addr - MIPS_KSEG0;
    long first = paddr/PAGE_SIZE;
    for(i=0;i<npages;i++){
        freepage(first+i);
    }
}


