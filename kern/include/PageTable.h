//
// Created by attil on 10/11/2020.
//

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_
#include "opt-pagetable.h"
#include <types.h>
#include <spinlock.h>

typedef struct _P {
    paddr_t *p_frames;
    char *control;
    unsigned int length;
    vaddr_t base_vaddr;
    struct spinlock pagetable_lock;
    //aggiungere struct spinlock pagetable_spinlock;

}pagetable;


pagetable *pagetable_init(int length, vaddr_t vaddr_base);

int pagetable_addentry(pagetable *pg,vaddr_t vaddr,paddr_t paddr,char flag);

int pagetable_getpaddr(pagetable *pg,vaddr_t vaddr, paddr_t *paddr);

void  pagetable_destroy(pagetable *pg);

#endif
