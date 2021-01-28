#ifndef _VM_STATS_H_
#define _VM_STATS_H_

extern int leakage;
extern int tlb_f;  /* total number of tlb faults */
extern int tlb_ff; /* total number of tlb faults with free space in tlb */
extern int tlb_fr; /* total number of tlb faults that needed replacement */
extern int tlb_i;  /* TODO: total number of tlb invalidations */
extern int tlb_r;  /* total number of tlb reloads */
extern int pf_z;   /* total number of tlb misses that required a new page to be zero filled */
extern int pf_d;   /* total number of tlb misses that required a page to be loaded from disk */

extern int pf_e;   /* total number of tlb misses that required a new page to be zero filled */
#endif /* _VM_STATS_H_ */