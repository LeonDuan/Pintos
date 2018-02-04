#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/synch.h"

struct list free_frames;
struct list frame_table;
struct lock frame_table_lock;

struct frame_table_entry {
	void * frame; /* which frame does this entry represent */
	struct supp_page_table_entry * spte; /* what page this frame corresponds to */
	struct list_elem elem;
	int clock_dirty; /*dirty bit for clock eviction alg*/
};

void frame_init (void);
void* frame_alloc (struct supp_page_table_entry *spte);
void frame_free (void *frame);
//void frame_add_to_table (void *frame, struct page *spte);
void frame_evict (void);

#endif /* vm/frame.h */
