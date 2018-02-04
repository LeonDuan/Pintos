#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/frame.h"
#include "filesys/off_t.h"
#include "threads/thread.h"
#define INSWAP 1
#define INFRAME 2
#define INFILE 3
#define INSTACK 4
#define STACK_THRESH 32
//8MB
#define MAX_STACK_SIZE 0x800000 

struct supp_page_table_entry {
	struct thread* owner;
	void *upage; /* user virtual address */
	int status; /*page currently in swap or in a frame */
	int swap_table_idx;
	bool pin;
	bool writable;

	// file info
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;

	struct hash_elem elem;
	struct lock load_lock;
};

void page_table_init (struct hash *spt);
void page_table_destroy (struct hash *spt);
bool page_map_to_frame(void* addr, void* sp, bool unpin);
bool load_page (struct supp_page_table_entry *spte);
bool grow_stack (struct hash *spt, void *va, bool unpin);
void page_unpin(struct hash *spt, void* upage);

bool page_add (struct hash *spt, void *upage, int status, 
	struct file *file, off_t ofs, uint32_t read_bytes,
	uint32_t zero_bytes, bool writable);
bool page_swap (void *page, int swap_index);
struct supp_page_table_entry *page_find(struct hash *spt, void *va);


/* Hash functions */
unsigned spte_hash_func(const struct hash_elem *elem, void *aux);
bool     spte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);
void     spte_destroy_func(struct hash_elem *elem, void *aux);


#endif /* vm/page.h */
