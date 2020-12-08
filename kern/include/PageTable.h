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
    uint16_t *control;
    //unsigned int occupied_frame;
    unsigned int length;
    paddr_t pbase;
    struct spinlock pagetable_lock;
    int* old_count;
    paddr_t *p_pages;

}pagetable;


//FLAG FORMAT: bit 0 = valid/invalid, bit 1 = dirty, bit 2 = write allowed

int pagetable_init(int length);

int pagetable_addentry(vaddr_t vaddr,paddr_t paddr,pid_t pid,uint16_t flag);

int pagetable_getpaddr(vaddr_t vaddr, paddr_t *paddr,pid_t *pid,uint16_t *flag);

void pagetable_remove_entries(pid_t pid);

/*void pagetable_remove_entry(pid_t pid, uint16_t control, vaddr_t v_page);*/

void pagetable_remove_entry(int replace_index);

int pagetable_change_flag(paddr_t paddr,uint16_t flag);

void pagetable_destroy(void);

/*int pagetable_replacement(vaddr_t temp,paddr_t paddr,pid_t pid,uint16_t flag);*/

int pagetable_replacement(pid_t pid);

vaddr_t pagetable_getVaddrByIndex(int index);

paddr_t pagetable_getPaddrByIndex(int index);

pid_t pagetable_getPidByIndex(int index);



#endif
