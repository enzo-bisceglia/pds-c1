#ifndef _PT_H_
#define _PT_H_


#include <uio.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>


#define VADDR_TO_PTEN(vaddr) (vaddr >> 12)
#define PRESENT_BIT 1
#define KERNEL_BIT  0
#define SetBit(A,k)     ( A[(k/8)] |= (1 << (k%8)) )
#define TestBit(A,k)    ( A[(k/8)] & (1 << (k%8)) )
#define ClearBit(A,k)   ( A[(k/8)] &= ~(1 << (k%8)) )

/* 2GB virtual address space, 4KB page size => 1M entries in page table */

extern struct pt_t* ipt;

struct pt_t {
    struct pte_t* v;
    int pt_size;
    struct spinlock ipt_splk;
};

struct pte_t {
    vaddr_t vaddr;
    pid_t pid;
    unsigned char flags[1];
};

struct pt_t*
pt_init(long size);

/* write a page of data in physical memory, reading from filesystem.
    - addrspace is needed in uiomove to satisfy a kassert (since we're moving data from kernel to user space) 
    - vnode contains information on how to do things in the file system
    - offset (within the file) of the read operation
    - vaddr is the virtual address in which the data will be written
    - chunk is how much data is remained (to move)

int page_write(struct addrspace* as, struct vnode* v, off_t offset, vaddr_t vaddr, size_t chunk, int is_executable);
*/

int
page_walk(vaddr_t vaddr, pid_t pid, int* ix);

void
setXbit(unsigned char flags[], int x, int val);

int
isSetX(unsigned char flags[], int x);

#endif /* _PT_H_ */