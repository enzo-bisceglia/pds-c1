/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <current.h>
#include "opt-synch.h"
#include <mips/trapframe.h>
/*
 * simple proc management system calls
 */
void
sys__exit(int status)
{
#if OPT_SYNCH
    struct proc* proc = curthread->t_proc; /* later curthread becomes NULL and t_proc unreachable */
    proc->status = status;
    proc_remthread(curthread); /* otherwise due to race conditions proc_destroy could happen before thread is detached */
    V(proc->waitsem);
#else
    /* get address space of current process and destroy */
    struct addrspace *as = proc_getas();
    as_destroy(as);
    (void) status; // TODO: status handling
#endif
    /* thread exits. proc data structure will be lost */
    thread_exit();

    panic("thread_exit returned (should not happen)\n");

}

#if OPT_SYNCH
pid_t
sys_getpid(void){
    return curproc->pid;
}

pid_t
sys_waitpid(pid_t pid, userptr_t wstatus, int options){

    struct proc* p;
    
    if (options!=0){
        kprintf("sys_waitpid does not support options\n");
        return -1;
    }
    p = get_proc_with_pid(pid);
    if (p==NULL)
        return -1;
    *(int*)wstatus = proc_wait(p);

    return pid;
}

static
void
fork_wrap(void* tf, unsigned long u){
    (void)u;
    enter_forked_process((struct trapframe* )tf);
}

pid_t
sys_fork(struct trapframe* tf_src){
    
    struct proc* proc;
    struct addrspace *as;
    //struct trapframe* tf;
    int result;
    /* Create a process for the new program to run in. */
    proc = proc_create_runprogram("forked");
    if (proc == NULL) {
		return -1;
	}

    /* now the trapframe for the forked process is set */
    /* who should set the address space? current process or forked one (by passing *as as parameter) ? */
    as_copy(curproc->p_addrspace, &as);
    proc_setas(as);

    result = thread_fork("forked",
            proc,
            fork_wrap,
            tf_src, 0);
            
    thread_yield();

    if (result){
        kprintf("thread_fork failed: %s\n", strerror(result));
		proc_destroy(proc);
		return result;
    }

    return 0;

}
#endif
