//
// Created by attil on 10/11/2020.
//

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_
#include "opt-pagetable.h"
#include <types.h>
#include <spinlock.h>


struct pte_t {
    vaddr_t vaddr;
    pid_t pid;
    int old_count;
    unsigned char flags;
    //    TLB_INDEX   ?     ?
    // 0x[ 000000 ] [ 0 ] [ 0 ]

};

struct pt_t {
    struct pte_t * v;
    unsigned int length;
    struct spinlock pt_lock;
};


int pagetable_init(unsigned int length);

int pagetable_addentry(vaddr_t vaddr, paddr_t paddr, pid_t pid, unsigned char flags);

/* ricerca lineare di una pagina all'interno della inverted page table.
 * Ritorna 1 in caso di successo e i parametri vengono aggiornati.
 * Ritorna 0 in caso di fallimento. */
int pagetable_getpaddr(vaddr_t vaddr, paddr_t* paddr, pid_t pid, unsigned char* flags);

void pagetable_remove_entries(pid_t pid);

void pagetable_remove_entry(int replace_index);

void pagetable_destroy(void);

int pagetable_replacement(pid_t pid);

vaddr_t pagetable_getVaddrByIndex(int index);

paddr_t pagetable_getPaddrByIndex(int index);

pid_t pagetable_getPidByIndex(int index);

unsigned char pagetable_getFlagsByIndex(int index);

void pagetable_setTlbIndex(int index, unsigned char val);

void pagetable_setFlagsAtIndex(int index, unsigned char val);

#endif
