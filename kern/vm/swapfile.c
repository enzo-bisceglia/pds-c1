#include "swapfile.h"
#include <lib.h>
#include <vm.h>

static swapfile *sw;

int swapfile_init(int length){
    int i;
    sw = (swapfile *) kmalloc(sizeof(swapfile));
    if(sw==NULL){
        return 0;
    }
    sw->v_pages = kmalloc(sizeof(vaddr_t)*length);
    if(sw->v_pages==NULL) return 0;

    sw->p_pages = kmalloc(sizeof(paddr_t)*length);
    if(sw->p_pages==NULL) return 0;

    sw->pids = kmalloc(sizeof(pid_t)*length);
    if(sw->pids==NULL) return 0;

    sw -> control = kmalloc(sizeof(uint16_t)*length);
    if(sw ->control==NULL) return 0;


    for(i=0;i<length;i++){
        sw ->control[i] = 0;
	    sw->pids[i] = -1;
	    sw->v_pages[i] = 0x0;
        sw->p_pages[i] = alloc_kpages(1);
    }
    sw -> pbase = ram_getfirst() & PAGE_FRAME;
    sw -> length = length;
    spinlock_init(&sw->pagetable_lock);
    return 1;
}
//swap-out
int swapfile_addentry(vaddr_t vaddr,paddr_t paddr,pid_t pid,uint16_t flag){
    unsigned int frame_index;
    unsigned int i;
    
    //struct addrspace* as;

    if (vaddr>MIPS_KSEG0) return -1;
    //vaddr_t relative_vaddr = vaddr & PAGE_FRAME;
    //paddr &= PAGE_FRAME;
    //paddr = paddr - sw->pbase;
    //unsigned int frame_index = (int) paddr/PAGE_SIZE;

    //cerco il primo libero
    for (i=0; i<sw->length; i++){
        if(sw->pids[i]==-1){
            frame_index = i;
            break;
        }
    }
    KASSERT(frame_index < sw->length);
    spinlock_acquire(&sw->pagetable_lock);
    memcpy((void *)sw->p_pages[frame_index], (void *)vaddr, PAGE_SIZE);
    sw -> v_pages[frame_index] = vaddr;
    sw -> p_pages[frame_index] = paddr;
    sw ->control[frame_index] =flag;
    sw->pids[frame_index] = pid;
    spinlock_release(&sw->pagetable_lock);
    return 1;
}

//swap-in
int swapfile_getpaddr(vaddr_t vaddr, paddr_t *paddr,pid_t *pid,uint16_t *flag){
    unsigned int i;
    //vaddr_t relative_vaddr = vaddr & PAGE_FRAME;
    spinlock_acquire(&sw->pagetable_lock);
    paddr_t p =-1;
    for(i=0;i<sw->length;i++){
        if(sw->v_pages[i]==vaddr && sw -> pids[i]==*pid){
            //p = (paddr_t) (i * PAGE_SIZE) + sw->pbase;
            p = sw->p_pages[i];	
            memcpy((void*)vaddr, (void *)sw->p_pages[i],PAGE_SIZE);
            break;
        }
    }
    if((int) p == -1) {
    	spinlock_release(&sw->pagetable_lock);
	return 0;
	}
    *paddr = p;
    *pid = sw->pids[i];
    *flag = sw->control[i];
    spinlock_release(&sw->pagetable_lock);
    (void) paddr;
    return 1;
}