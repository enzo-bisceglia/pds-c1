#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include "coremap.h"


struct spinlock freemem_lock = SPINLOCK_INITIALIZER;
struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static unsigned char* freeRamFrames = NULL;
static unsigned long* allocSize = NULL;
static int nRamFrames = 0;
static int allocTableActive = 0;

int
coremap_bootstrap(int ram_size){

    int i;
    
    nRamFrames = (ram_size/PAGE_SIZE);
    freeRamFrames = kmalloc(sizeof(unsigned char)*nRamFrames);
    if (freeRamFrames == NULL)
        return ENOMEM;
    allocSize = kmalloc(sizeof(unsigned long)*nRamFrames);
    if (allocSize == NULL) {
        freeRamFrames = NULL;
        return ENOMEM;
    }
    
    for (i=0; i<nRamFrames; i++){
        freeRamFrames[i] = (unsigned char)0;
        allocSize[i] = 0;
    }

    spinlock_acquire(&freemem_lock);
    allocTableActive = 1;
    spinlock_release(&freemem_lock);

    return 0;
}

int
vm_is_active(void){

    int active;

    spinlock_acquire(&freemem_lock);
    active = allocTableActive;
    spinlock_release(&freemem_lock);
    return active;
}

static paddr_t
getfreeppages(unsigned long npages){

    paddr_t addr;
    long i, first, found, np = (long)npages;

    if (!vm_is_active())
        return 0;

    spinlock_acquire(&freemem_lock);
    for (i=0, first=found=-1; i<nRamFrames; i++) {
        if (freeRamFrames[i]) {
            if (i==0 || !freeRamFrames[i-1])
                first = i;
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

int
freeppages(paddr_t paddr){
    long i, np, first = paddr/PAGE_SIZE;
    
    KASSERT(allocSize!=NULL);
    KASSERT(nRamFrames>first);
    np = allocSize[first];

    spinlock_acquire(&freemem_lock);
    for (i=first; i<first+np; i++) {
        freeRamFrames[i] = (unsigned char)1;
    }
    spinlock_release(&freemem_lock);
    
    return 1;
}

paddr_t
getppages(unsigned long npages){
    
    paddr_t addr;
    
    /* try freed pages first */
    addr = getfreeppages(npages);
    if (addr == 0){
        /* call stealmem */
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
    }
    if (addr!=0 && vm_is_active()) {
        spinlock_acquire(&freemem_lock);
        allocSize[addr/PAGE_SIZE] = npages;
        spinlock_release(&freemem_lock);
    }

    return addr;
}
