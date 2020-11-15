//
// Created by attil on 10/11/2020.
//

#ifndef PGOS161_PAGETABLE_H
#define PGOS161_PAGETABLE_H

struct page_table{
    paddr_t *p_frames;
    char *control;
    unsigned int length;
    vaddr_t base_vaddr;
    //aggiungere struct spinlock pagetable_spinlock;
} typedef pagetable;

pagetable *pagetable_init(int length, vaddr_t vaddr_base);

int pagetable_addentry(pagetable *pg,int vaddr,paddr_t paddr,char flag);

int pagetable_removeaddr(pagetable *pg,vaddr_t vaddr);

int pagetable_getpaddr(pagetable *pg,vaddr_t vaddr);

char pagetable_getcontrolbit(pagetable *pg, vaddr_t vaddr);

int pagetable_getlength(pagetable *pg);

void pagetable_destroy(pagetable *pg);

#endif //PGOS161_PAGETABLE_H
