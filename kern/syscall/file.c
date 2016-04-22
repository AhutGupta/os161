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
		kprintf("Bad pointer...\n");
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
		kprintf("Bad pointer...\n");
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
		kprintf("Bad pointer...\n");
		return EFAULT;
	}

	uio_kinit(&iov, &u, buf, buflen, 0, UIO_READ);
	u.uio_segflg = UIO_USERSPACE;
	u.uio_space = curthread->t_proc->p_addrspace;
	result = vfs_getcwd(&u);
    *retval = result;

    return result;
 }

pid_t getpid(){

	return curthread -> t_pid;
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

void child_forkentry(void* c_tf, unsigned long c_addrspace) { 

	struct trapframe *new_tf;
	struct addrspace * new_addrspace;

	new_tf = (struct trapframe *) c_tf;
	new_addrspace = (struct addrspace *) c_addrspace;

	new_tf->tf_a3 = 0;
	new_tf->tf_v0 = 0;
	new_tf->tf_epc += 4;

	curthread->t_proc->p_addrspace = new_addrspace;
	as_activate();

	struct trapframe tf_new = *new_tf;
	mips_usermode(&tf_new);
}

pid_t sys_fork(struct trapframe *parent_tf, int *retval){
	
	struct addrspace *child_addrspace;
	int result;
	const char *child = "child thread";
	struct proc *child_proc = (struct proc *) kmalloc(sizeof(struct proc*));
	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe*));
	
	if(child_proc == NULL){
		kfree(child_tf);
		return ENOMEM;
	}
	if(child_tf == NULL){
		kfree(child_proc);
		return ENOMEM;
	}

	child_tf = parent_tf;
	result = as_copy(curthread->t_proc->p_addrspace, &child_addrspace);
	if (result) {
		return ENOMEM;
	}

	result = thread_fork(child, child_proc, child_forkentry, (void *) child_tf, (unsigned long) child_addrspace);
	if (result) {
		kprintf("thread_fork failed.....\n");
		return ENOMEM;
	}

	//Return child PID for parent
	*retval = 1;

	return 0;


//.......................

}

pid_t waitpid(pid_t pid, userptr_t retstatus, int flags, pid_t *retval)
{
	int status; 
	int result;

	result = pid_wait(pid, &status, flags, retval);
	if (result) {
		return result;
	}

	return copyout(&status, retstatus, sizeof(int));
}

int sbrk(intptr_t amount, int *retval){
	struct addrspace *as = proc_getas();

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
    }

}



