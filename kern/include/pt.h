//
// Created by attil on 10/11/2020.
//

#ifndef _PT_H_
#define _PT_H_

#include <types.h>
#include <spinlock.h>

struct pte_t {
    vaddr_t vaddr;
    pid_t pid;
    int old_count;
    unsigned char flags;
    //    TLB_INDEX   ?     READONLY
    // 0x[ 000000 ] [ 0 ] [ 0 ]

};

struct pt_t {
    struct pte_t * v;
    unsigned int length;
    struct spinlock pt_lock;
};


int pagetable_init(unsigned int length);

int pagetable_addentry(vaddr_t vaddr, paddr_t paddr, pid_t pid, unsigned char flags);

/* Ricerca la pagina virtuale identificata da indirizzo 'vaddr' e posseduta dal processo con pid 'pid' nella IPT. 
 * Ritorna valore 1 in caso di successo e in 'paddr' l'indirizzo fisico, oltre al byte contenente i flags associati.
 * Ritorna valore 0 in caso di insuccesso.
*/
int pagetable_getpaddr(vaddr_t vaddr, paddr_t* paddr, pid_t pid, unsigned char* flags);

void pagetable_remove_entries(pid_t pid);

void pagetable_remove_entry(int replace_index);

void pagetable_destroy(void);


int pagetable_replacement(pid_t pid);

/* Ritorna indirizzo virtuale di pagina della entry i-esima nella IPT */
vaddr_t pagetable_getVaddrByIndex(int index);

/* Ritorna indirizzo fisico di pagina della entry i-esima nella IPT */
paddr_t pagetable_getPaddrByIndex(int index);

/* Ritorna pid della entry i-esima nella IPT */
pid_t pagetable_getPidByIndex(int index);

/* Ritorna i flag della entry i-esima nella IPT */
unsigned char pagetable_getFlagsByIndex(int index);

/* Salva l'indice in cui si trova una certa pagina in tlb nei 6 bit alti del byte contenente i flag */
void pagetable_setTlbIndex(int index, unsigned char val);

/* Setta a 'val' il flag della pagina fisica con indice 'index' nella IPT */
void pagetable_setFlagsAtIndex(int index, unsigned char val);

void lru_update_cnt(void);
#endif /* _PT_H_ */
