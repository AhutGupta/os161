/*
#include <vm.h>
#include <types.h>
#include <spinlock.h>

struct coremap_entry{
	struct page_struct *page;
	unsigned int cpu_index : 4;
	int tlb_index : 7;
	int block_length : 4;
	bool is_allocated : 1;
	bool is_pinned : 1;

};

struct page_struct{
	paddr_t ps_padder;
	off_t ps_swapaddr;
	struct spinlock ps_spinlock;
};

void initializeCoremap();
*/
