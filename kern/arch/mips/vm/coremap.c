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
struct spinlock *core_lock;
unsigned sizeofmap;


void initializeCoremap(void){

	size_t npages, csize;

	paddr_t firstpaddr, lastpaddr;
	ram_getsize(&firstpaddr, &lastpaddr);
	core_lock = (struct spinlock*)PADDR_TO_KVADDR(firstpaddr);
	spinlock_init(core_lock);
	size_t locksize = sizeof(struct spinlock);
	locksize = ROUNDUP(locksize, PAGE_SIZE);
	firstpaddr+= locksize;
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
	sizeofmap = npages;
	for(unsigned i = 0; i < npages; i++){
		coremap[i].ps_padder = firstpaddr+(i*PAGE_SIZE);
		coremap[i].ps_swapaddr = 0;
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
	for(unsigned i = 0; i<sizeofmap; i++){
		if(coremap[i].is_allocated)
			page_count = 0;
		else
			page_count++;
		if(page_count == npages){
			int start_index = i+1-npages;
			spinlock_acquire(core_lock);
			//palloc = true;
			coremap[start_index].block_length = npages;
			for(unsigned j = start_index; j<=i; j++){
				coremap[j].is_allocated = 1;
			}
			spinlock_release(core_lock);
			vaddr_t returnaddr = PADDR_TO_KVADDR(coremap[start_index].ps_padder);
			return returnaddr;
		}
	}
	return 0;
}
void free_kpages(vaddr_t addr){
	paddr_t page_ad = KVADDR_TO_PADDR(addr);
	unsigned i=0;
	kprintf("Buffer...\n");
	for (i=0; i<sizeofmap; i++){
		if(coremap[i].ps_padder == page_ad)
			break;
	}
	KASSERT(coremap[i].is_allocated);
	int j = i;
	spinlock_acquire(core_lock);
	while(j<coremap[i].block_length){
		coremap[j].is_allocated = 0;
		coremap[j].block_length = 0;
		j++;
	}
	spinlock_release(core_lock);

}

void
vm_bootstrap(void)
{
	initializeCoremap();
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
