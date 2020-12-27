//
// Created by attil on 10/11/2020.
//

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_
#include "opt-pagetable.h"
#include <types.h>
#include <spinlock.h>

#define PRESENT 1
#define SWAPPED 2

struct pte_t {
    vaddr_t vaddr;
    pid_t pid;
    int old_count;
    unsigned char flags;
};

struct pt_t {
    struct pte_t * v;
    unsigned int length;
    struct spinlock pt_lock;
};

/*
typedef struct _P {
    vaddr_t *v_pages;
    pid_t *pids;
    uint16_t *control;
    //unsigned int occupied_frame;
    unsigned int length;
    struct spinlock pagetable_lock;
    int* old_count;
    paddr_t *p_pages;

}pagetable;
*/

//FLAG FORMAT: bit 0 = valid/invalid, bit 1 = dirty, bit 2 = write allowed

int pagetable_init(unsigned int length);

int pagetable_addentry(vaddr_t vaddr, paddr_t paddr, pid_t pid, unsigned char flags);

/* ricerca lineare di una pagina all'interno della inverted page table.
 * Ritorna 1 in caso di successo e i parametri vengono aggiornati.
 * Ritorna 0 in caso di fallimento. */
int pagetable_getpaddr(vaddr_t vaddr, paddr_t* paddr, pid_t* pid, unsigned char* flags);

void pagetable_remove_entries(pid_t pid);

/*void pagetable_remove_entry(pid_t pid, uint16_t control, vaddr_t v_page);*/

void pagetable_remove_entry(int replace_index);

int pagetable_change_flags(paddr_t paddr, unsigned char flags);

void pagetable_destroy(void);

/*int pagetable_replacement(vaddr_t temp,paddr_t paddr,pid_t pid,uint16_t flag);*/

int pagetable_replacement(pid_t pid);

vaddr_t pagetable_getVaddrByIndex(int index);

paddr_t pagetable_getPaddrByIndex(int index);

pid_t pagetable_getPidByIndex(int index);

uint16_t pagetable_getControlByIndex(int index);


#endif
