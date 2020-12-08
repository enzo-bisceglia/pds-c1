#define _PAGETABLE_H_
#include "opt-pagetable.h"

#include <types.h>
#include <spinlock.h>

typedef struct _PS {
    vaddr_t *v_pages;
    pid_t *pids;
    uint16_t *control;
    unsigned int length;
    paddr_t pbase;
    struct spinlock pagetable_lock;
    paddr_t* p_pages;
}swapfile;

int swapfile_init(int length);

int swapfile_addentry(vaddr_t vaddr,paddr_t paddr,pid_t pid,uint16_t flag);

int swapfile_getpaddr(vaddr_t vaddr, paddr_t *paddr,pid_t *pid,uint16_t *flag);