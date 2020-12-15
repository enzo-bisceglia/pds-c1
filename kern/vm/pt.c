#include <types.h>
#include <kern/errno.h>
#include <lib.h>

#include "pt.h"

struct pte*
pt_init(void){
    struct pte* pt;

    pt = kmalloc(PT_LENGTH*sizeof(struct pte));
    if (pt==NULL){
        return pt;
    }

    return pt;
}


int
page_write(struct addrspace* as, struct vnode* v, off_t offset, vaddr_t vaddr, size_t chunk, int is_executable){
    
    struct iovec iov;
	struct uio u;
	int result;

	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = PAGE_SIZE;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = chunk;
	u.uio_offset = offset;
	u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = as;

	result = VOP_READ(v, &u);
	if (result) {
		return result;
	}

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.
	 */
#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n",
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif

	return result;

}

void
pt_destroy(struct pte* pt){
    kfree(pt);
}