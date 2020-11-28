#include <types.h>
#include <lib.h>
#include "pt.h"

struct pte* pt_init(void){
    struct pte* pt;

    pt = kmalloc(PT_LENGTH*sizeof(struct pte));
    if (pt==NULL){
        return pt;
    }
    return pt;
}

void pt_destroy(struct pte* pt){
    kfree(pt);
}