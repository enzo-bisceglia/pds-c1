#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>
#include <kern/errno.h>

#define TLB_MAP_SIZE 8

int tlb_map_init(void);
void tlb_clean_entry(int index);
void tlb_write_entry(int *index, uint32_t ehi, uint32_t elo);

#endif /* _VM_TLB_H_ */