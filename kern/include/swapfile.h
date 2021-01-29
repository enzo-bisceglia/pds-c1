#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

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


/*
 * Fa swapout della pagina virtuale identificata da indirizzo 'vaddr' e pid 'pid' nello swapfile.
 * Per la scrittura su file si appoggia all' indirizzo fisico in 'paddr'. 
*/
int swapfile_swapout(vaddr_t vaddr, paddr_t paddr, pid_t pid, unsigned char flags);

/*
 * Ricerca la pagina virtuale identificata da indirizzo 'vaddr' e posseduta dal processo con pid 'pid' nella
 * partizione di swap, appoggiandosi ad una mappa in memoria dello swapfile. Se l'esito è positivo, alloca una
 * pagina per compiere il suo lavoro. Se necessario (memoria terminata oppure altri vincoli non rispettati) fa
 * swapout di una pagina in memoria.
*/
int swapfile_swapin(vaddr_t vaddr, paddr_t *paddr, pid_t pid, struct addrspace *as);

#endif /* _SWAPFILE_H_ */
