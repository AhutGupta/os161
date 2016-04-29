// File containing File System Call functions
// Author- Ahut Gupta

#include <fs.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <array.h>
#include <lib.h>
#include <current.h>
#include <thread.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <types.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <synch.h>
#include <kern/unistd.h>
#include <uio.h>
#include <proc.h>
#include <syscall.h>
#include <trapframe.h>
#include <addrspace.h>

#define HEAP_MAX 0x40000000
#include <spl.h>
#include <kern/wait.h>

int initial_ftable(void){
	struct vnode *vin, *vout, *verr;
	char *stdin, *stdout, *stderr;
	int result;
	stdin = kstrdup("con:");
	//stdin
	result = vfs_open(stdin, O_RDONLY, 0664, &vin);
	if(result){
		kfree(stdin);
		return result;
	}
	curthread->file_table[0] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[0]->flags = O_RDONLY;
	curthread->file_table[0]->offset = 0;
	curthread->file_table[0]->ref_count = 1;
	curthread->file_table[0]->filelock = lock_create(stdin);
	curthread->file_table[0]->vnode = vin;
	
	//stdout
	stdout = kstrdup("con:");
	result = vfs_open(stdout, O_WRONLY, 0664, &vout);
	if(result){
		kfree(stdin);
		kfree(stdout); 
		return result;
	}
	curthread->file_table[1] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[1]->flags = O_WRONLY;
	curthread->file_table[1]->offset = 0;
	curthread->file_table[1]->ref_count = 1;
	curthread->file_table[1]->filelock = lock_create(stdout);
	curthread->file_table[1]->vnode = vout;

	//stderr
	stderr = kstrdup("con:");
	result = vfs_open(stderr, O_WRONLY, 0664, &verr);
	if(result){
		kfree(stdin);
		kfree(stdout); 
		kfree(stderr);
		return result;

	}
	curthread->file_table[2] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[2]->flags = O_WRONLY;
	curthread->file_table[2]->offset = 0;
	curthread->file_table[2]->ref_count = 1;
	curthread->file_table[2]->filelock = lock_create(stderr);
	curthread->file_table[2]->vnode = verr;

	return 0;
}


int sys_open(const_userptr_t filename, int flags, mode_t mode, int *retval){
	struct vnode* fileobject;
	int fd = 3, result;
	char *name = (char *) kmalloc(sizeof(char)*PATH_MAX);
	size_t name_len;
	
	//Stat struct for getting the size of the file. (VOP_STAT)
	struct stat file_stat;

	result = copyinstr((const_userptr_t) filename, name, PATH_MAX, &name_len);
	if(result) {
		kprintf("FILE OPEN - copyinstr failed- %d\n",result);
		kfree(name);
		return result;
	}

	// Check for Flags.
	//Not Done***********************************
	if(!((flags & O_ACCMODE) == O_RDONLY || (flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR)){
		return EINVAL;
	}
	

	while (curthread->file_table[fd] != NULL ) {
		fd++;
	}

	if(fd == OPEN_MAX) {
		kfree(name);
		return ENFILE;
	}

	curthread->file_table[fd] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	if(curthread->file_table[fd] == NULL)
	{
        	kfree(name);
        	return ENFILE; 
      	} 
	  	
  	result = vfs_open(name, flags, mode, &fileobject);
	if(result){
		kfree(name);
 		return result;
	}
	
	curthread->file_table[fd]->flags = flags;
	
	if(flags==O_APPEND){
		int stat_err = VOP_STAT(fileobject, &file_stat);
		if(stat_err)
			return stat_err;
		curthread->file_table[fd]->offset = file_stat.st_size;
	}
	
	else
		curthread->file_table[fd]->offset = 0;


	curthread->file_table[fd]->ref_count = 1;
	curthread->file_table[fd]->filelock=lock_create(name);
	curthread->file_table[fd]->vnode = fileobject;

	*retval = fd;
	return 0;

}

int sys_close(int fd, int *retval){
	
	if(fd>=OPEN_MAX || fd<0 || curthread->file_table[fd]==NULL){
		return EBADF;
	}

	curthread->file_table[fd]->ref_count--;

	if(curthread->file_table[fd]->ref_count == 0){

		lock_destroy(curthread->file_table[fd]->filelock);
		vfs_close(curthread->file_table[fd]->vnode);
		kfree(curthread->file_table[fd]);
		curthread->file_table[fd]=NULL;

	}

	*retval = 0;
	return 0;

}

int sys_read(int fd, void *buf, size_t buflen, int *retval){

	struct iovec iov;
	struct uio u;
	void *mem_buf[buflen];
	int result;

	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL || curthread->file_table[fd]->flags == O_WRONLY){

		kprintf("Invalid File Descriptor\n");
		return EBADF;
	}

	result = copyin((const_userptr_t) buf, mem_buf, buflen);
	if(result){
		kprintf("In sys_read. Bad pointer...\n");
		return EFAULT;
	}

	lock_acquire(curthread->file_table[fd]->filelock);

	iov.iov_ubase = (userptr_t) buf;
	iov.iov_len = buflen;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_offset = curthread->file_table[fd]->offset;
	u.uio_resid = buflen;
	u.uio_rw = UIO_READ;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_space = curthread->t_proc->p_addrspace;	

	result = VOP_READ(curthread->file_table[fd]->vnode, &u);
	if(result){
		kprintf("Error during read\n");
		lock_release(curthread->file_table[fd]->filelock);
		return EIO;
	}

	*retval = buflen-u.uio_resid;
	curthread->file_table[fd]->offset = u.uio_offset;

	lock_release(curthread->file_table[fd]->filelock);
	return 0;

}

int sys_write(int fd, const void *buf, size_t nbytes, int *retval){

	struct iovec iov;
	struct uio u;
	void *mem_buf[nbytes];
	int result;

	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL || curthread->file_table[fd]->flags == O_RDONLY){

		kprintf("in sys_write.................\n");
		kprintf("Invalid File Descriptor\n");
		return EBADF;
	}

	result = copyin((const_userptr_t) buf, mem_buf, nbytes);
	if(result){
		kprintf("In sys_write. Bad pointer...\n");
		return EFAULT;
	}

	//ENOSPC condition

	lock_acquire(curthread->file_table[fd]->filelock);

	//uio_kinit(&iov, &u, (void *) buf, nbytes, curthread->file_table[fd]->offset, UIO_WRITE);
	iov.iov_ubase = (userptr_t) buf;
	iov.iov_len = nbytes;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_offset = curthread->file_table[fd]->offset;
	u.uio_resid = nbytes;
	u.uio_rw = UIO_WRITE;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_space = curthread->t_proc->p_addrspace;	

	// if(curthread->file_table[fd]->vnode == NULL){
	// 	panic("Null pointer in sys_write......\n");
	// 	return -1;
	// }
	// if(curthread->file_table[fd]->vnode->vn_ops == NULL){
	// 	panic("VN_OPS is null for vnode object....\n");
	// 	return -1;
	// }

	result = VOP_WRITE(curthread->file_table[fd]->vnode, &u);
	if(result){
		kprintf("Error during write\n");
		lock_release(curthread->file_table[fd]->filelock);
		return EIO;
	}

	*retval = nbytes-u.uio_resid;
	curthread->file_table[fd]->offset = u.uio_offset;

	lock_release(curthread->file_table[fd]->filelock);
	return 0;
}


int sys_lseek(int fd, off_t pos, int whence, off_t *retval_high){

	off_t new_pos;
	struct stat file_stat;
	int stat_err;

	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL){
		kprintf("Invalid File Descriptor\n");
		return EBADF;
	}

	if(!(VOP_ISSEEKABLE(curthread->file_table[fd]->vnode))){
		kprintf("Seek not allowed on this file..\n");
		return ESPIPE;
	}

	lock_acquire(curthread->file_table[fd]->filelock);

	switch(whence){
		
		case SEEK_SET:
		new_pos = pos;
		break;

		case SEEK_CUR:
		new_pos = curthread->file_table[fd]->offset+pos;
		break;

		case SEEK_END:
		stat_err = VOP_STAT(curthread->file_table[fd]->vnode, &file_stat);
		if(stat_err){
			lock_release(curthread->file_table[fd]->filelock);
			return stat_err;
		}
		new_pos = file_stat.st_size + pos;
		break;

		default:
		kprintf("WHENCE is invalid\n");
		lock_release(curthread->file_table[fd]->filelock);
		return EINVAL;

	}

	if(new_pos<0){
		kprintf("Resulting seek value is negative\n");
		lock_release(curthread->file_table[fd]->filelock);
		return EINVAL;
	}

	// int result = VOP_TRYSEEK(curthread->file_table[fd]->vnode, new_pos);
	// if(result){
	// 	kprintf("Error During VOP_TRYSEEK\n");
	// 	lock_release(curthread->file_table[fd]->filelock);
	// 	return result;

	// }

	curthread->file_table[fd]->offset = new_pos;
	*retval_high = new_pos;
	lock_release(curthread->file_table[fd]->filelock);

	return 0;
}


int sys_chdir(const char *pathname){
	char *name = (char *) kmalloc(sizeof(char)*PATH_MAX);
	size_t name_len;
	int result;

	result = copyinstr((const_userptr_t) pathname, name, PATH_MAX, &name_len);
	if(result) {
		kprintf("Wrong Path - copyinstr failed- %d\n",result);
		kfree(name);
		return EFAULT;
	}

	//ERROR CODES!!!!

	result = vfs_chdir(name);
	if(result){
		kfree(name);
		return ENOTDIR;
	}

	return 0;

}


int sys__getcwd(char *buf, size_t buflen, int *retval){

	struct iovec iov;
	struct uio u;
	void *mem_buf = (void *) kmalloc(buflen);
	int result;

	result = copyin((const_userptr_t) buf, mem_buf, buflen);
	if(result){
		kprintf("In getCWD. Bad pointer...\n");
		return EFAULT;
	}

	uio_kinit(&iov, &u, buf, buflen, 0, UIO_READ);
	u.uio_segflg = UIO_USERSPACE;
	u.uio_space = curthread->t_proc->p_addrspace;
	result = vfs_getcwd(&u);
    *retval = result;

    return result;
 }

int sys_dup2(int oldfd, int newfd, int *retval){

	int result;

	if(oldfd < 0 || newfd < 0 ){
		return EBADF;
	}

	if(oldfd > OPEN_MAX || newfd > OPEN_MAX){
		return  EMFILE;
	}

	lock_acquire(curthread->file_table[oldfd]->filelock);

	if(curthread->file_table[newfd] != NULL) {
		result = sys_close(newfd, retval);
		if(result) {
			kprintf("dup2 - sys_close failed!!\n");
			return -1;
		}
	}
	else {
		curthread->file_table[newfd] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	}


	curthread->file_table[newfd]->flags = curthread->file_table[oldfd]->flags;
	curthread->file_table[newfd]->offset = curthread->file_table[oldfd]->offset;
	curthread->file_table[newfd]->ref_count = curthread->file_table[oldfd]->ref_count;
	curthread->file_table[newfd]->filelock = curthread->file_table[oldfd]->filelock;
	curthread->file_table[newfd]->vnode = curthread->file_table[oldfd]->vnode;

	*retval = newfd;
	return 0;

}

pid_t getpid(){

	return curproc -> pid;
}

pid_t sys_fork(struct trapframe *parent_tf, int *retval){
	int result = 0;
	// child_proc = (struct proc *) kmalloc(sizeof(struct proc));
	struct proc *child_proc = proc_create("Child Process");
	if(child_proc == NULL){
		*retval = -1;
		return ENPROC;
	}

	lock_acquire(proc_lock);
	pid_t c_pid = givepid();
	if(c_pid == -1){
		lock_release(proc_lock);
		return ENOMEM;
	}
	child_proc->ppid = curproc->pid;
	child_proc->pid = c_pid;
	proc_table[c_pid] = child_proc;
	lock_release(proc_lock);


	struct addrspace *child_addrspace;

	result = as_copy(curproc->p_addrspace, &child_addrspace);

	if(result){
		return ENOMEM;
	}

	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));

	if(child_tf == NULL){
		return ENOMEM;
	}

	*child_tf = *parent_tf;

	result = thread_fork("Child Thread", child_proc, entrypoint, (struct trapframe *) child_tf, (unsigned long) child_addrspace);

	if(result){
		return ENOMEM;
	}

	*retval = child_proc->pid;

	return 0;
}

void entrypoint(void* data1, unsigned long data2) {
	struct trapframe *tf, new_tf;
	struct addrspace * addr;
	
	tf = (struct trapframe *) data1;
	addr = (struct addrspace *) data2;
	
	tf->tf_a3 = 0;
	tf->tf_v0 = 0;
	tf->tf_epc += 4;
	
	curproc->p_addrspace = addr;
	as_activate();
	new_tf = *tf;
	mips_usermode(&new_tf);
}

void sys_exit(int exitcode){

	pid_t j;
	//int r;
	//for(int i=0; i<OPEN_MAX; ++i){
	//	sys_close(i, &r);
	//}

	for(j=PID_MIN; j<MAX_PROC; j++){
			if(curproc->pid == j){
				//kprintf("EXIT. Found my PID: %d, AND PPID: %d", j, curproc->ppid);
				break;
			}
		}	

	//kprintf("Exited from FOR loop...\n");

	if(j == MAX_PROC || proc_table[j] == NULL){
		kprintf("This Process ID %d is not there in Table.", curproc->pid);
		panic("Did not find this Process in the proc_table\n");
	}

	if (curproc->p_addrspace) {
		//struct addrspace *as = curproc->p_addrspace;
		struct addrspace *as = proc_setas(NULL);
		as_destroy(as);
		//kprintf("Cleared Addrspace in sys_exit...\n");
	}

	// exitcode = 0;
	proc_table[j]->exitcode = _MKWAIT_EXIT(exitcode);
	proc_table[j]->exited = true;
	//kprintf("Was able to set the exitcode...\n");
	splhigh();
	V(proc_table[j]->exitsem);



	// for(i=PID_MIN; i<MAX_PROC; i++){
	// 	if(curproc->ppid == i){
	// 		kprintf("EXIT..My PPID is: %d", i);
	// 		break;
	// 	}
	// }

	// if(i == MAX_PROC){
	// 	kprintf("i= MAXPROC....\n");
	// 	curproc->p_numthreads--;
	// 	proc_destroy(curproc);
	// } 
	// else if(proc_table[i]-> exited == false){
	// 	for(j=PID_MIN; j<MAX_PROC; j++){
	// 		if(curproc->pid == j){
	// 			kprintf("EXIT. Found my PID: %d", j);
	// 			break;
	// 		}
	// 	}	

	// 	kprintf("Exited from FOR loop...\n");

	// 	if(j == MAX_PROC){
	// 		kprintf("This Process ID %d is not there in Table.", curproc->pid);
	// 	}

	// 	if (curproc->p_addrspace) {
	// 		struct addrspace *as = proc_setas(NULL);
	// 		//= curproc->p_addrspace;
	// 		// curproc->p_addrspace = NULL;
	// 		// as_activate();
	// 		as_destroy(as);
	// 		kprintf("Cleared Addrspace...\n");

	// 	}

	// 	// exitcode = 0;
	// 	proc_table[j]->exitcode = _MKWAIT_EXIT(exitcode);
	// 	proc_table[j]->exited = true;
	// 	splhigh();
	// 	V(proc_table[j]->exitsem);
	// 	kprintf("Able to set V.....\n");

	// } else {
	// 	kprintf("Else Condition....\n");
	// 	curproc->p_numthreads--;
	// 	proc_destroy(curproc);
	// } 

	thread_exit();
}

pid_t sys_waitpid(pid_t pid, int *status, int options, int *retval){
	int result;

	if(options != 0){
		return EINVAL;
	}

	if(status == NULL){
		return EFAULT;
	}

	if(pid < PID_MIN){
		return EINVAL;
	}

	if(pid > MAX_PROC){
		return ESRCH;
	}

	if(pid == curproc->pid){
		return ECHILD;
	}

	if(pid == curproc->ppid){
		return ECHILD;
	}

	// for(demo=proc_table; demo->pid != pid || demo == NULL; demo=demo->next);

	pid_t i;
	for(i=PID_MIN; i<MAX_PROC; i++){
		if(i==pid){
			kprintf("In WAITPID. Found my child's PID: %d ", i);
			break;
		}
	}

	if(i == MAX_PROC){
		return ESRCH;
	}

	if(proc_table[i]->ppid != curproc->pid){
		return ECHILD;
	}

	if(proc_table[i]->exited == false){
		P(proc_table[i]->exitsem);
		kprintf("In WaitPID. Got P for PID: %d", i);
	}
	

	result = copyout(&(proc_table[i]->exitcode), (userptr_t) status, sizeof(int));

	if(result){
		return EFAULT;
	}

	*retval = pid;
	kprintf("Destroying PROC now......\n");

	//proc_destroy(proc_table[i]);

	sem_destroy(proc_table[i]->exitsem);
	//spinlock_cleanup(*(proc_table[i]->p_lock));
	kprintf("Everything good. freeing..\n");
	kfree(proc_table[i]->p_name);
	kfree(proc_table[i]);


	return 0;
}

int sbrk(intptr_t amount, int *retval){
	/*struct addrspace *as = proc_getas();

    if (amount == 0){
    	*retval = as->heap_end;
    	return 0;
    }
    else if(amount<0){
    	if ((long)as->heap_end + (long)amount >= (long)as->heap_start) {
            as->heap_end += amount;
            *retval = as->heap_end;
            return 0;
        }
    	*retval = -1;
    	return EINVAL;
    }
    else{
    	
    	if ((as->heap_end+amount) < (USERSTACK-STACKPAGES * PAGE_SIZE) && (as->heap_end+amount) < (as->heap_start+HEAP_MAX)) {
        *retval = as->heap_end;
        as->heap_end += amount;
        return 0;
    	}

    	*retval = -1;
    	return ENOMEM;
    } */
    (void) amount;
    (void) *retval;
    return 0;
}

int sys_execv(const char *program, char **user_args){


	/* int res, length, index = 0, i = 0;
	struct lock *lock;
	lock = lock_create("Lock for Execv");
	struct vnode *vnode;
	vaddr_t entrypoint, stackptr;

	lock_acquire(lock);

	struct addrspace *temporary;
	temporary = curproc->p_addrspace;

	if(program == NULL || user_args == NULL){
		return EFAULT;
	}

	char *pname;
	size_t size;

	pname = (char *) kmalloc(sizeof(char) *PATH_MAX);
	res = copyinstr((const_userptr_t) program, pname, PATH_MAX, &size);

	if(res){
		kfree(pname);
		return EFAULT;
	}

	if(size == 1){
		kfree(pname);
		return EINVAL;
	}

	char **arguments = (char **) kmalloc(sizeof(char **));
	res = copyin((const_userptr_t) user_args, arguments, sizeof(char **));
	if(res){
		kfree(pname);
		kfree(arguments);
		return EFAULT;
	}

	while(user_args[i] != NULL ){
		arguments[i] = (char *) kmalloc(sizeof(char) * PATH_MAX);
		res = copyinstr((const_userptr_t) user_args[i], arguments[i], PATH_MAX,
				&size);
		if (res) {
			kfree(pname);
			kfree(arguments);
			return EFAULT;
		}
		i++;
	}

	arguments[i] = NULL;

	res = vfs_open(pname, O_RDONLY, 0, &vnode);
	if(res) {
		kfree(pname);
		kfree(arguments);
		return res;
	}

	if(temporary != NULL){
		as_destroy(temporary);
		temporary = NULL;
	}

	KASSERT(temporary == NULL);
	if((temporary=as_create()) == NULL){
		kfree(pname);
		kfree(arguments);
		vfs_close(vnode);
		return ENOMEM;
	}

	as_activate();
	res = load_elf(vnode, &entrypoint);
	if(res){
		kfree(pname);
		kfree(arguments);
		return res;
	}
	vfs_close(vnode);

	res = as_define_stack(temporary, &stackptr);
	if(res){
		kfree(pname);
		kfree(arguments);
		return res;
	}

	while(arguments[index] != NULL){
		char * arg;
		length = strlen(arguments[index]) + 1;

		int original_length = length;
		if(length % 4 != 0){
			length = length + (4 - length%4);
		}

		arg = kmalloc(sizeof(length));
		arg = kstrdup(arguments[index]);

		for(int i=0; i<length; ++i){
			if(i >= original_length){
				arg[i] = '\0';
			}
			else {
				arg[i] = arguments[index][i];
			}
		}

		stackptr = stackptr - length;

		res = copyout((const void *) arg, (userptr_t) stackptr, (size_t) length);
		if(res){
			kfree(pname);
			kfree(arguments);
			kfree(arg);
			return res;
		}

		kfree(arg);
		arguments[index] = (char *)stackptr;

		index++;
	}

	if(arguments[index] == NULL){
		stackptr = stackptr - 4*sizeof(char);
	}

	for(int i = index-1; i >= 0; --i){
		stackptr = stackptr - sizeof(char*);
		res = copyout((const void *) (arguments+i), (userptr_t) stackptr, (sizeof(char *)));

		if(res){
			kfree(pname);
			kfree(arguments);
			return res;
		}
	}
	
	kfree(pname);
	kfree(arguments);

	lock_release(lock);

	enter_new_process(index, (userptr_t) stackptr, NULL, stackptr, entrypoint);
	panic("panic enter_new_process");
	return EINVAL; */
	(void) user_args;

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open((char *)program, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	proc_setas(NULL);
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	//curproc->pid = PID_MIN;


	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}


