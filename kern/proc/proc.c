/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <synch.h>
#include <kern/errno.h>
#include <kern/wait.h>


/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
struct lock* proc_lock;

pid_t givepid(void) {
	for(pid_t i = PID_MIN; i<MAX_PROC; i++){
		if(proc_table[i]==NULL)
			return i;
	}
	return -1;
}

/*
 * Create a proc structure.
 */

struct proc *proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	proc->ppid = 0;
	proc->exited = false;
	proc->exitcode = 0;
	proc->self = curthread;

	struct semaphore *sem;
	sem = sem_create("child", 0);
	proc->exitsem = sem;
	
	/* if(proc_table == NULL){
		count_proc = 1;
		proc_table = (struct proc_table *)kmalloc(sizeof(struct proc_table));
		proc_table->next = NULL;
		proc->pid = count_proc;
		proc_table->pid = proc->pid;
		proc_table->proc = proc;
	} else {
		for(temporary=proc_table; temporary->next!= NULL; temporary=temporary->next);
			temporary->next = (struct proc_table *)kmalloc(sizeof(struct proc_table));
			temporary->next->next = NULL;
			proc->ppid = curproc->pid;
			proc->pid = givepid();
			temporary->next->proc = proc;
			temporary->next->pid = proc->pid;
		if(temporary->next->pid > PID_MAX) {
			lock_release(proc_lock);
			return NULL;
		} 
	} */

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	lock_acquire(proc_lock);
	kfree(proc_table[proc->pid]);
	lock_release(proc_lock);

	sem_destroy(proc->exitsem);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	proc_lock = lock_create("proctable_lock");
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}


// Self


// static struct pidinfo * pidinfo_create(pid_t pid, pid_t ppid)
// {
// 	struct pidinfo *pi;

// 	KASSERT(pid != INVALID_PID);

// 	pi = kmalloc(sizeof(struct pidinfo));
// 	if (pi==NULL) {
// 		return NULL;
// 	}

// 	pi->pid = pid;
// 	pi->ppid = ppid;
// 	pi->exited = false;

// 	return pi;
// }

// static void inc_nextpid(void)
// {
// 	KASSERT(lock_do_i_hold(pidlock));

// 	nextpid++;
// 	if (nextpid > __PID_MAX) {
// 		nextpid = __PID_MIN;
// 	}
// }

// static void pi_put(pid_t pid, struct pidinfo *pi)
// {
// 	KASSERT(lock_do_i_hold(pidlock));

// 	KASSERT(pid != INVALID_PID);

// 	KASSERT(myStruct[pid % __PID_MAX] == NULL);
// 	myStruct[pid % __PID_MAX] = pi;
// 	nprocs++;
// }

// pid_t pid_alloc()
// {
// 	struct pidinfo *pi;
// 	pid_t pid;
// 	int count;

// 	KASSERT(curthread->t_pid != INVALID_PID);

// 	/* lock the table */
// 	lock_acquire(pidlock);

// 	if (nprocs == __PID_MAX) {
// 		lock_release(pidlock);
// 		return EAGAIN;
// 	}

// 	count = 0;
// 	while (myStruct[nextpid % __PID_MAX] != NULL) {

// 		KASSERT(count < __PID_MAX*2+5);
// 		count++;

// 		inc_nextpid();
// 	}

// 	pid = nextpid;

// 	pi = pidinfo_create(pid, curthread->t_pid);
// 	if (pi==NULL) {
// 		lock_release(pidlock);
// 		return ENOMEM;
// 	}

// 	pi_put(pid, pi);
// 	inc_nextpid();
// 	lock_release(pidlock);
// 	return pid;
// }


// static void pidinfo_destroy(struct pidinfo *pi)
// {
// 	KASSERT(pi->exited==true);
// 	KASSERT(pi->ppid==INVALID_PID);
// 	cv_destroy(pi->cv_process);
// 	kfree(pi);
// }

// static void pi_drop(pid_t pid)
// {
// 	struct pidinfo *pi;

// 	KASSERT(lock_do_i_hold(pidlock));

// 	pi = myStruct[pid % __PID_MAX];
// 	KASSERT(pi != NULL);
// 	KASSERT(pi->pid == pid);

// 	pidinfo_destroy(pi);
// 	myStruct[pid % __PID_MAX] = NULL;
// 	nprocs--;
// }

// static struct pidinfo * pi_get(pid_t pid)
// {
// 	struct pidinfo *pi;

// 	KASSERT(pid>=0);
// 	KASSERT(pid != INVALID_PID);
// 	KASSERT(lock_do_i_hold(pidlock));

// 	pi = myStruct[pid % __PID_MAX];
// 	if (pi==NULL) {
// 		return NULL;
// 	}
// 	if (pi->pid != pid) {
// 		return NULL;
// 	}
// 	return pi;
// }

// int pid_wait(pid_t theirpid, int *status, int flags, pid_t *ret)
// {
// 	struct pidinfo *them;

// 	KASSERT(curthread->t_pid != INVALID_PID);

// 	if (theirpid == curthread->t_pid) {
// 		return EINVAL;
// 	}
// 	if (theirpid == INVALID_PID || theirpid<0) {
// 		return EINVAL;
// 	}
// 	if (flags != 0 && flags != WNOHANG) {
// 		return EINVAL;
// 	}

// 	lock_acquire(pidlock);

// 	them = pi_get(theirpid);
// 	if (them==NULL) {
// 		lock_release(pidlock);
// 		return ESRCH;
// 	}

// 	KASSERT(them->pid==theirpid);

// 	if (them->ppid != curthread->t_pid) {
// 		lock_release(pidlock);
// 		return EPERM;
// 	}

// 	if (them->exited==false) {
// 		if (flags==WNOHANG) {
// 			lock_release(pidlock);
// 			KASSERT(ret!=NULL);
// 			*ret = 0;
// 			return 0;
// 		}
// 		cv_wait(them->cv_process, pidlock);
// 		KASSERT(them->exited==true);
// 	}

// 	if (status != NULL) {
// 		*status = them->exitcode;
// 	}
// 	if (ret != NULL) {
// 		*ret = theirpid;
// 	}

// 	them->ppid = 0;
// 	pi_drop(them->pid);

// 	lock_release(pidlock);
// 	return 0;
// }


// New Self

// void destroy_process(pid_t pid) {
// 	struct proc_table *demo, *temporary;
// 	for(demo = proc_table; demo->next->pid != pid; demo=demo->next);
// 		temporary = demo->next;
// 		demo->next = demo->next->next;
// 	kfree(temporary->proc);
// 	kfree(temporary);
// }