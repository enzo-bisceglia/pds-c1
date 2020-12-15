#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <vm.h>
#include <coremap.h>

/*
 * Wrap ram_stealmem in a spinlock.
 */

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;

static unsigned char *freeRamFrames = NULL;
static unsigned long *allocSize = NULL;
static int nRamFrames = 0;
static int framesNotTracked = 0;
int framesFreed = 0;
static int allocTableActive = 0;

static int isTableActive (void) {
	int active;
	spinlock_acquire(&freemem_lock);
	active = allocTableActive;
	spinlock_release(&freemem_lock);
	return active;
}

void
coremap_bootstrap(void) {

	int i;
	nRamFrames = ((int)ram_getsize())/PAGE_SIZE;  
	/* alloc freeRamFrame and allocSize */
	freeRamFrames = kmalloc(sizeof(unsigned char)*nRamFrames);
	if (freeRamFrames==NULL) return;  
	allocSize = kmalloc(sizeof(unsigned long)*nRamFrames);
	if (allocSize==NULL) {    
    /* reset to disable this vm management */
    	freeRamFrames = NULL; return;
  	}
	for (i=0; i<nRamFrames; i++) {
		freeRamFrames[i] = (unsigned char)0;
		allocSize[i]     = 0;
	}
	/* get first address that can be tracked */
	framesNotTracked = ((int)allocSize+(sizeof(unsigned long)*nRamFrames)-MIPS_KSEG0)/4096;
	spinlock_acquire(&freemem_lock);
	allocTableActive = 1;
	spinlock_release(&freemem_lock);

}

static
paddr_t 
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
getppages(unsigned long npages) {

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
	framesFreed-=npages;
	return addr;
}

int 
freeppages(paddr_t addr){
	long i, first, np;	

	if (!isTableActive()) return 0;
	first = addr/PAGE_SIZE;
	KASSERT(allocSize!=NULL);
    KASSERT(nRamFrames>first);
	np = (long)allocSize[first];

	spinlock_acquire(&freemem_lock);
	for (i=first; i<first+np; i++) {
    	freeRamFrames[i] = (unsigned char)1;
  	}
  	spinlock_release(&freemem_lock);
	framesFreed+=np;
	return 1;
}