#include <vm.h>
#include <types.h>
#include <spinlock.h>

struct coremap_entry{
	paddr_t ps_padder;
	off_t ps_swapaddr;
	unsigned int cpu_index : 4;
	int tlb_index : 7;
	int block_length : 4;
	bool is_allocated : 1;
	bool is_pinned : 1;
};

void initializeCoremap(void);

vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);