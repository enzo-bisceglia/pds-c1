#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>

#include "pt.h"


struct pt_t*
pt_init(long size) {
	struct pt_t* ipt;
	struct pte_t *v;
	int i;

	ipt = kmalloc(sizeof(struct pt_t));
	if (ipt==NULL){
		return NULL;
	}

	ipt->v = kmalloc(size*sizeof(struct pte_t));
	if (ipt->v==NULL){
		ipt = NULL;
		return NULL;
	}

	ipt->pt_size = size;

	v = ipt->v;
	for (i=0; i<size; i++)
		v[i].flags[0] = 0;
	
	return ipt;
}

int
isSetX(unsigned char flags[], int x){
	return TestBit(flags, x);
}

void
setXbit(unsigned char flags[], int x, int val){
	val==1 ? SetBit(flags, x) : ClearBit(flags, x);
}

int
page_walk(vaddr_t faultaddress, pid_t pid, int *ix){
	int i, res=1;

	spinlock_acquire(&ipt->ipt_splk);
	*ix=-1;
	for (i=0; i<ipt->pt_size; i++){
		if (ipt->v[i].pid == pid && ipt->v[i].vaddr == faultaddress){
			res = isSetX(ipt->v[i].flags, PRESENT_BIT) ?
					0 	/* page in memory, not in tlb */
					: 1;/* page allocated, waiting to be loaded */
			*ix = i;
			break;
		}
	}
	spinlock_release(&ipt->ipt_splk);
	/* page was evicted */
	return res;
}
