/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <machine/tlb.h>
#include <spl.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->pages=NULL;
	as->regionlist=NULL;
	as->heap_start=0;
	as->heap_end=0;
	as->loading=0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	//Copy the pagetable
	struct pagetable_entry *pagelist = old->pages;
	while(pagelist!=NULL){
		struct pagetable_entry *newpage = kmalloc(sizeof(struct pagetable_entry));
		newpage = pagelist;
		newpage->next=NULL;
		if(newas->pages==NULL){
			newas->pages = newpage;
		}
		else{
			struct pagetable_entry *page_pos = newas->pages;
			while(page_pos->next!=NULL){
				page_pos=page_pos->next;
			}
			page_pos->next = newpage;
		}
		pagelist=pagelist->next;
	}

	//Copy the regions
	struct regions *region_list = old->regionlist;
	while(region_list!=NULL){
		struct regions *newregion = kmalloc(sizeof(struct regions));
		newregion = region_list;
		newregion->next=NULL;
		if(newas->regionlist==NULL){
			newas->regionlist = newregion;
		}
		else{
			struct regions *reg_pos = newas->regionlist;
			while(reg_pos->next!=NULL){
				reg_pos=reg_pos->next;
			}
			reg_pos->next = newregion;
		}
		region_list=region_list->next;
	}

	//Copy stack and register
	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;
	newas->loading = old->loading;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	while(as->regionlist!=NULL){
		free_kpages(as->regionlist->vbase);
		struct regions *temp = as->regionlist;
		kfree(temp);
		as->regionlist = as->regionlist->next;
	}

	while(as->pages!=NULL){
		free_kpages(as->pages->vaddr);
		struct pagetable_entry *temp = as->pages;
		kfree(temp);
		as->pages = as->pages->next;
	}

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	int spl = splhigh();

	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	size_t npages;
	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	//Update heap start and end
	if (as->heap_start < vaddr) {
		as->heap_start = vaddr;
		as->heap_end = vaddr+memsize;
	}

	struct regions *nextregion;
	if(as->regionlist==NULL){
		as->regionlist = kmalloc(sizeof(struct regions));
		nextregion = as->regionlist;
	}
	else{
		nextregion = as->regionlist;
		while(nextregion->next!=NULL){
			nextregion = nextregion->next;
		}
		nextregion->next = kmalloc(sizeof(struct regions));
		nextregion = nextregion->next;
	}

	nextregion->vbase = vaddr;
	nextregion->npages = npages;
	nextregion->permissions[0] = readable;
	nextregion->permissions[1] = writeable;
	nextregion->permissions[3] = executable;
	nextregion->next = NULL;
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	//Set Loading to 1. Checked during vm_fault
	as->loading = 1;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	as->loading=0;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

struct pagetable_entry * get_pte(struct addrspace *as, vaddr_t vbase){
	struct pagetable_entry *pte = as->pages;
	if(pte==NULL)
		return NULL;
	
	int v = (vbase>>12)&0xfffff;

	while(pte!=NULL){
		int page_v = pte->vaddr;
		if(page_v == v){
			return pte;
		}

		pte = pte->next;
	}

	return NULL;
}

void pte_insert(struct addrspace *as, vaddr_t vbase, vaddr_t pbase, bool perm[3]){
	vaddr_t v = vbase>>12;
	vaddr_t p = pbase>>12;
	struct pagetable_entry *newpage = kmalloc(sizeof(struct pagetable_entry));
	newpage->vaddr = v;
	newpage->paddr = p;
	for(int i=0; i<3; i++)
		newpage->permissions[i] = perm[i];
	newpage->next = NULL;

	struct pagetable_entry* curr = as->pages;

	if(curr==NULL){
		as->pages = newpage;
		return;
	}
	while(curr->next!=NULL)
		curr=curr->next;

	curr->next = newpage;
	return;

}


