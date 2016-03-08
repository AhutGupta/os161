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
#include <types.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <synch.h>
#include <kern/unistd.h>
#include <uio.h>
#include <proc.h>
#include <syscall.h>


int initial_ftable(void){
	//panic('%s', "Entered......................");
	struct vnode *vin, *vout, *verr;
	char *console, *lock_name;
	int result;
	
	console = kstrdup("con:");
	//KASSERT(console != NULL);
	lock_name = kstrdup("con_lock");
	// stdout = kstrdup("con:");
	// stderr = kstrdup("con:");

	//kfree(stdin);
	//kfree(stdout);
	//kfree(stderr);

	//stdin
	result = vfs_open(console, O_RDONLY, 0, &vin);
	if(result){
		//kfree(console);
		//panic('%d %s\n', &result, "Panic during stdin.................");
		kfree(lock_name);


		return result;
	}
	curthread->file_table[0] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[0]->flags = O_RDONLY;
	curthread->file_table[0]->offset = 0;
	curthread->file_table[0]->ref_count = 1;
	curthread->file_table[0]->filelock = lock_create(lock_name);
	curthread->file_table[0]->vnode = vin;
	
		//stdout
	result = vfs_open(console, O_WRONLY, 0, &vout);
	if(result){
		kfree(console);
		//panic('%d %s\n', &result, "Panic during stdout.................");

		kfree(lock_name); 

		return result;
	}
	curthread->file_table[1] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[1]->flags = O_WRONLY;
	curthread->file_table[1]->offset = 0;
	curthread->file_table[1]->ref_count = 1;
	curthread->file_table[1]->filelock = lock_create(lock_name);
	curthread->file_table[1]->vnode = vout;

	//console = kstrdup("con:");
	KASSERT(console != NULL);
	//stderr
	result = vfs_open(console, O_WRONLY, 0, &verr);
	if(result){
		//panic('%d %s\n', &result, "Panic during stderr.................");

		kprintf("........................result = ");
		//kprintf(result);
		kfree(console);
		kfree(lock_name); 

		return result;

	}
	curthread->file_table[2] = (struct file_handle *)kmalloc(sizeof(struct file_handle*));
	curthread->file_table[2]->flags = O_WRONLY;
	curthread->file_table[2]->offset = 0;
	curthread->file_table[2]->ref_count = 1;
	curthread->file_table[2]->filelock = lock_create(lock_name);
	curthread->file_table[2]->vnode = verr;

//panic ("Got here\n\n\n");

	return 0;

}


int sys_open(const_userptr_t filename, int flags, int mode, int *retval){
	struct vnode* fileobject;
	int fd = 3, result;
	char *name = (char *) kmalloc(sizeof(char)*PATH_MAX);
	size_t name_len;
	

	//Stat struct for getting the size of the file. (VOP_STAT)
	struct stat *file_stat = (struct stat*) kmalloc(sizeof(struct stat*));
	

	result = copyinstr(filename, name, PATH_MAX, &name_len);
	if(result) {
		kprintf("FILE OPEN - copyinstr failed- %d\n",result);
		kfree(name);
		return result;
	}

	// Check for Flags.
	if(flags!=O_RDONLY || flags!=O_WRONLY || flags!= O_RDWR){
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
        	return ENFILE; 
      	} 
	  	
  	result = vfs_open(name, flags, mode, &fileobject);
	if(result)
    		return result;
	
	curthread->file_table[fd]->flags = flags;
	
	if(flags==O_APPEND){
		int stat_err = VOP_STAT(fileobject, file_stat);
		if(stat_err)
			return stat_err;
		curthread->file_table[fd]->offset = file_stat->st_size;
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

	struct iovec *readbuffer = (struct iovec*) kmalloc(sizeof(struct iovec*));
	struct uio *buffer = (struct uio*) kmalloc(sizeof(struct uio*));
	void *mem_buf = (void *) kmalloc(buflen);
	int result;

	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL || curthread->file_table[fd]->flags == O_WRONLY){

		kprintf("Invalid File Descriptor\n");
		return EBADF;
	}

	result = copyin((const_userptr_t) buf, mem_buf, IOV_MAX);
	if(result){
		kprintf("Bad pointer...\n");
		return EFAULT;
	}

	lock_acquire(curthread->file_table[fd]->filelock);

	uio_kinit(readbuffer, buffer, buf, buflen, curthread->file_table[fd]->offset, UIO_READ);
	buffer->uio_segflg = UIO_USERSPACE;
	buffer->uio_space = curthread->t_proc->p_addrspace;

	result = VOP_READ(curthread->file_table[fd]->vnode, buffer);
	if(result){
		kprintf("Error during read\n");
		kfree(mem_buf);
		lock_release(curthread->file_table[fd]->filelock);
		return EIO;
	}

	*retval = buflen-buffer->uio_resid;
	curthread->file_table[fd]->offset = buffer->uio_offset;

	kfree(mem_buf);
	lock_release(curthread->file_table[fd]->filelock);
	return 0;

}

int sys_write(int fd, const void *buf, size_t nbytes, int *retval){

	struct iovec *writebuffer = (struct iovec*) kmalloc(sizeof(struct iovec*));
	struct uio *buffer = (struct uio*) kmalloc(sizeof(struct uio*));
	void *mem_buf = (void *) kmalloc(nbytes);
	int result;

	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL || curthread->file_table[fd]->flags == O_RDONLY){

		kprintf("Invalid File Descriptor\n");
		return EBADF;
	}

	result = copyin((const_userptr_t) buf, mem_buf, IOV_MAX);
	if(result){
		kprintf("Bad pointer...\n");
		return EFAULT;
	}

	//ENOSPC condition

	lock_acquire(curthread->file_table[fd]->filelock);

	uio_kinit(writebuffer, buffer, (void *)buf, nbytes, curthread->file_table[fd]->offset, UIO_READ);
	buffer->uio_segflg = UIO_USERSPACE;
	buffer->uio_space = curthread->t_proc->p_addrspace;

	result = VOP_WRITE(curthread->file_table[fd]->vnode, buffer);
	if(result){
		kprintf("Error during write\n");
		kfree(mem_buf);
		lock_release(curthread->file_table[fd]->filelock);
		return EIO;
	}

	*retval = nbytes-buffer->uio_resid;
	curthread->file_table[fd]->offset = buffer->uio_offset;

	kfree(mem_buf);
	lock_release(curthread->file_table[fd]->filelock);
	return 0;
}

// off_t lseek(int fd, off_t pos, int whence){

// 	if(fd<0 || fd>OPEN_MAX || curthread->file_table[fd] == NULL){
// 		kprintf("Invalid File Descriptor\n")
// 		return EBADF;
// 	}

// 	if(fd <= 2){
// 		kprintf("Seek not allowed on console..\n");
// 		return ESPIPE;
// 	}

// 	if(!(whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END)){
// 		kprintf("Invalid Whence..\n");
// 		return EINVAL;
// 	}
// }