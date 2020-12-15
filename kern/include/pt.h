#ifndef _PT_H_
#define _PT_H_

#include <uio.h>
#include <addrspace.h>
#include <vnode.h>

#define PT_LENGTH 1048576
#define VADDR_TO_PTEN(vaddr) (vaddr >> 12)
#define PRESENT_BIT 0
#define SetBit(A,k)     ( A[(k/8)] |= (1 << (k%8)) )
#define TestBit(A,k)    ( A[(k/8)] & (1 << (k%8)) )
#define ClearBit(A,k)   ( A[(k/8)] &= ~(1 << (k%8)) )

/* 2GB virtual address space, 4KB page size => 1M entries in page table */

struct pte {
    paddr_t paddr;
    char* flags;
};

struct pte* pt_init(void);
/* write a page of data in physical memory, reading from filesystem.
    - addrspace is needed in uiomove to satisfy a kassert (since we're moving data from kernel to user space) 
    - vnode contains information on how to do things in the file system
    - offset (within the file) of the read operation
    - vaddr is the virtual address in which the data will be written
    - chunck is how much data is remained (to move)
*/
int page_write(struct addrspace* as, struct vnode* v, off_t offset, vaddr_t vaddr, size_t chunk, int is_executable);

void pt_destroy(struct pte *);
#endif /* _PT_H_ */