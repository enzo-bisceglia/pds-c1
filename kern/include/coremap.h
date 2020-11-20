#ifndef _COREMAP_H_
#define _COREMAP_H_

/* funzione di inizializzazione */
void coremap_bootstrap(void);
/* funzione per allocare frame al kernel */
paddr_t getppages(unsigned long npages);
/* funzione per deallocare frame dal kernel */
int freeppages(paddr_t addr);
#endif /* _COREMAP_H */
