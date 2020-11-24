#ifndef _VMSTATS_H_
#define _VMSTATS_H_

extern int tlb_faults;
extern int tlb_faults_free;
extern int tlb_faults_repl;
extern int tlb_inv;
extern int tlb_rel;     /*?*/
extern int pf_z;        /*?*/
extern int pf_disk;
extern int pf_elf;
extern int pf_swap;     /*?*/
extern int sf_writes;   /*?*/

#endif /* _VMSTATS_H_ */