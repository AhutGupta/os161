#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/coremap.h>

static struct coremap_entry *coremap;

void initializeCoremap(void){

	size_t npages, csize;

	paddr_t firstpaddr, lastpaddr;
	ram_getsize(&firstpaddr, &lastpaddr);
	npages = (lastpaddr - firstpaddr) / PAGE_SIZE;
	KASSERT(firstpaddr!=0);
	coremap = (struct coremap_entry*)PADDR_TO_KVADDR(firstpaddr);
	if(coremap == NULL){
		panic("Unable to create Coremap....\n");
	}
	csize = npages * sizeof(struct coremap_entry);
	csize = ROUNDUP(csize, PAGE_SIZE);
	firstpaddr+= csize;
	npages = (lastpaddr - firstpaddr) / PAGE_SIZE;

	for(size_t i = 0; i < npages; i++){
		coremap[i].page->ps_padder = firstpaddr+(i*PAGE_SIZE);
		coremap[i].cpu_index = 0;
		coremap[i].tlb_index = -1;
		coremap[i].block_length = 0;
		coremap[i].is_allocated = 0;
		coremap[i].is_pinned = 0;

	}
}

vaddr_t alloc_kpages(unsigned npages){
	unsigned page_count = 0;
	//bool palloc = false;
	for(unsigned i = 0; i<npages; i++){
		if(coremap[i].is_allocated)
			page_count = 0;
		else
			page_count++;
		if(page_count == npages){
			int start_index = i+1-npages;
			spinlock_acquire(coremap[start_index].page->ps_spinlock);
			//palloc = true;
			coremap[start_index].block_length = npages;
			for(unsigned j = start_index; j<=npages; j++){
				coremap[j].is_allocated = 1;
			}
			spinlock_release(coremap[start_index].page->ps_spinlock);
			return coremap[start_index].page->ps_padder;
		}
	}
	return 0;
}
void free_kpages(vaddr_t addr){
	paddr_t page_ad =  KVADDR_TO_PADDR(addr);
	int i;
	for (i=0; coremap[i].page->ps_padder!=page_ad; i++);
	KASSERT(coremap[i].is_allocated);
	int j = i;
	while(j<coremap[i].block_length){
		coremap[j].is_allocated = 0;
		coremap[j].block_length = 0;
		j++;
	}

}

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

unsigned
int
coremap_used_bytes() {

	/* Not yet */

	return 0;
}

void
vm_tlbshootdown_all(void)
{
	panic("You tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("You tried to do tlb shootdown?!\n");
}

int vm_fault(int faulttype, vaddr_t faultaddress){
	(void) faultaddress;
	(void) faulttype;
	return 0;
}
