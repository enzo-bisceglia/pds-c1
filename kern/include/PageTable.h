//
// Created by attil on 10/11/2020.
//

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_
#include "opt-pagetable.h"
#include <types.h>
#include <spinlock.h>


typedef struct _P {
    vaddr_t *v_pages;
    pid_t *pids;
    char *control;
    //unsigned int occupied_frame;
    unsigned int length;
    paddr_t pbase;
    struct spinlock pagetable_lock;


}pagetable;

//FLAG FORMAT: bit 0 = valid/invalid, bit 1 = dirty, bit 2 = write allowed

int pagetable_init(int length);

int pagetable_addentry(vaddr_t vaddr,paddr_t paddr,pid_t pid,char flag);

int pagetable_getpaddr(vaddr_t vaddr, paddr_t *paddr,pid_t *pid,char *flag);

void pagetable_remove_entries(pid_t pid);

void pagetable_destroy(void);

#endif
