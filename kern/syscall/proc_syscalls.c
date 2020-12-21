/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
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
fork_wrap(void *tf, unsigned long foo){
    (void)foo;
    enter_forked_process((struct trapframe*)tf);
    panic("enter_forked_process returned (should not happen)\n");
}

pid_t
sys_fork(struct trapframe* trap_tf, pid_t* retval){
    
    struct trapframe *tf;
    struct proc* proc;
    int result;

    /* Create a process for the new program to run in. */
    proc = proc_create_runprogram("forked");
    if (proc == NULL) {
		return ENOMEM;
	}
    /* Create an exact copy of the parent address space. as_activate will be called by thread_startup */
    result = as_copy(curproc->p_addrspace, &proc->p_addrspace, proc->pid);
    if (result) {
        proc_destroy(proc);
        return ENOMEM;
    }
    /* Trapframe is needed for the child when it has to jump to the instruction following this one */
    tf = kmalloc(sizeof(struct trapframe));
    if (tf == NULL){
        proc_destroy(proc);
        return ENOMEM;
    }

    memcpy(tf, trap_tf, sizeof(struct trapframe));

    result = thread_fork("forked",
            proc,
            fork_wrap,
            (void*)tf, (unsigned long)0);

    if (result){
        kprintf("thread_fork failed: %s\n", strerror(result));
		proc_destroy(proc);
        kfree(tf);
		return ENOMEM;
    }

    *retval = proc->pid;
    return 0; 

}
#endif
