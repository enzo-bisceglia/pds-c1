#ifndef _PT_H_
#define _PT_H_

#define PT_LENGTH 1048576
#define VADDR_TO_PTEN(vaddr) (vaddr >> 12)
/* 2GB virtual address space, 4KB page size => 1M entries in page table */

struct pte {
    paddr_t paddr;
    int valid;
};

struct pte* pt_init(void);
#endif /* _PT_H_ */