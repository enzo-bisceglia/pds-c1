#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>

#include <vm.h>
#include <uio.h>
#include <vnode.h>
#include <elf.h>
#include <vfs.h>
#include "PageTable.h"
#include "swapfile.h"

static swapfile *sw;
static struct spinlock swapfile_lock;
unsigned int sw_length;
struct vnode* swapstore;
int fd;

static const char swapfilename[] = "emu0:SWAPFILE";

int swapfile_init(int length){
    int i;
    char path[sizeof(swapfilename)];

    sw = (swapfile *) kmalloc(sizeof(swapfile)*length);
    if(sw==NULL){
        return 1;
    }
    
    for(i=0;i<length;i++){
        sw[i].control = 0;
	    sw[i].pids = -1;
	    sw[i].v_pages = 0x0;
    }
    
    sw_length = length;
    strcpy(path, swapfilename);
    fd = vfs_open(path, O_RDWR | O_CREAT ,0, &swapstore); //ricordati che il file è da dimensionare a 9MB
    if (fd){
        kprintf("swap: error %d opening swapfile %s\n", fd, swapfilename);
        kprintf("swap: Please create swapfile/swapdisk\n");
        panic("swapfile_init can't open swapfile");
    }
    spinlock_init(&swapfile_lock);
    return 0;
}

int swapfile_swapout(vaddr_t vaddr,paddr_t paddr,pid_t pid, uint16_t flag){
    unsigned int frame_index,i, err;
    struct iovec iov;
    struct uio ku;

    if (vaddr>MIPS_KSEG0) return -1;

    //CERCO IL PRIMO FRAME LIBERO IN CUI POTER FARE SWAPOUT    
    for(i=0; i<sw_length; i++){
        if(sw[i].pids==-1){
            frame_index = i;
            break;
        }
    }

    //FACCIO SWAPOUT
    uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE, frame_index*PAGE_SIZE, UIO_WRITE);
    err = VOP_WRITE(swapstore, &ku);
    if (err) {
			panic(": Write error: %s\n",strerror(err));
	}
    sw[frame_index].v_pages = vaddr;
    sw[frame_index].control =flag;
    sw[frame_index].pids = pid;

    pagetable_remove_entry(paddr/PAGE_SIZE); 
    return 1;
}

int swapfile_swapin(vaddr_t vaddr, paddr_t *paddr,pid_t *pid, struct addrspace *as){
    
    unsigned int i;
    int indexR;
    paddr_t p =-1;
    struct iovec iov;
    struct uio ku;

    for(i=0;i<sw_length;i++){
        if(sw[i].v_pages==vaddr && sw[i].pids==*pid){
            p = getppages(1);
            //non c'è spazio
            if (p==0 || as->count_proc>=MAX_PROC_PT){
                indexR = pagetable_replacement(*pid);
                swapfile_swapout(pagetable_getVaddrByIndex(indexR), indexR*PAGE_SIZE, *pid, pagetable_getControlByIndex(indexR)); 
                pagetable_remove_entry(indexR);
                bzero((void*)(indexR*PAGE_SIZE), 1);
                uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(indexR*PAGE_SIZE), PAGE_SIZE, i*PAGE_SIZE, UIO_READ);
                VOP_READ(swapstore, &ku); 
                pagetable_addentry(vaddr, indexR*PAGE_SIZE, *pid, sw[*paddr/PAGE_SIZE].control);
                *paddr = indexR*PAGE_SIZE;
                return 1;
            }
            *paddr = p;
            uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(p), PAGE_SIZE, i*PAGE_SIZE, UIO_READ);
            VOP_READ(swapstore, &ku);  
            pagetable_addentry(vaddr, p, *pid, sw[i].control);
            sw[i].pids = 0;
          
        }
    }
    return 0;
}