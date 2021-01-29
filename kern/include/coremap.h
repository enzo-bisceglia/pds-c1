#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <spinlock.h>

int vm_is_active(void);
int coremap_bootstrap(int ram_size);
int freeppages(paddr_t paddr);
paddr_t getppages(unsigned long npages);
 
#endif /* _COREMAP_H_ */