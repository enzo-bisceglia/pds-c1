#include "vm_tlb.h"
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <mips/tlb.h>

unsigned char* tlb_map;
static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;

int
tlb_map_init(void) {

    int i;

    tlb_map = kmalloc(TLB_MAP_SIZE*sizeof(unsigned char));
    if (tlb_map == NULL){
        return 1;
    }

    for (i=0; i<TLB_MAP_SIZE; i++)
        tlb_map[i]=0;

    return 0;
}

static
int
tlb_get_rr_victim(void){
    int victim;
    static unsigned int next_victim=0;
    victim = next_victim;
    next_victim = (next_victim+1)%NUM_TLB;
    return victim;
}

static
int
tlb_get_fresh_index(void){
	int i, j;
	unsigned char ix = 1;

	spinlock_acquire(&tlb_lock);
	for (i=0; i<TLB_MAP_SIZE; i++){
		if ((tlb_map[i]&0xff)==0xff)
			continue;
		for (j=0; j<8; j++){
			if (((~tlb_map[i])&ix)==ix){
                spinlock_release(&tlb_lock);
				return (8*i)+j;
			}
			else
				ix<<=1;
		}
	}
    /* NO MORE TLB ENTRIES AVAILABLE -> REPLACEMENT*/
    i = tlb_get_rr_victim();
	spinlock_release(&tlb_lock);
    return i;
}

void
tlb_write_entry(int* index, uint32_t ehi, uint32_t elo){

    int spl;

    if (*index==-1)
        *index = tlb_get_fresh_index();

    spl = splhigh();

    tlb_write(ehi, elo, *index);
    tlb_map[*index/8] |= 1 << (*index%8); 

    splx(spl);
}

void
tlb_clean_entry(int index){

    int spl;

    spl = splhigh();

    tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    tlb_map[index/8] &= ~(1 << (index%8)); 

    splx(spl);

}
