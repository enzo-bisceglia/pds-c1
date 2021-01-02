#define _PAGETABLE_H_
#include "opt-pagetable.h"

#include <types.h>
#include <spinlock.h>
#include <addrspace.h>
#define O_CREAT       4
#define O_RDWR        2      /* Open for read and write */


typedef struct _PS {
    vaddr_t v_pages; 
    pid_t pid; 
    unsigned char flags;
}swapfile;

int swapfile_init(int length);

unsigned int swapfile_getfreefirst();

int swapfile_swapout(vaddr_t vaddr,paddr_t paddr,pid_t pid, unsigned char flags);

int swapfile_swapin(vaddr_t vaddr, paddr_t *paddr,pid_t pid, struct addrspace *as);
