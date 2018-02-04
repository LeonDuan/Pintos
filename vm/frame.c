#include "vm/frame.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "vm/page.h"


enum palloc_flags flags;

void frame_init (void){
	void* page = NULL;
	list_init(&frame_table);
	list_init(&free_frames);
	lock_init(&frame_table_lock);
	while ((page = palloc_get_page(PAL_USER)) != NULL) {
		struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
		fte->frame = page;
		fte->clock_dirty = 1;
		list_push_back(&free_frames,&fte->elem);
	}
}
void* frame_alloc (struct supp_page_table_entry *spte){
	lock_acquire(&frame_table_lock);

	/* if no frames available, evict frame */
	if(list_empty(&free_frames)) {
		frame_evict();
	}

	/* when frame available, get from frame list */
	struct list_elem *elem = list_pop_front(&free_frames);
	struct frame_table_entry *fte = list_entry(elem, struct frame_table_entry, elem);
	list_remove(elem);

	/* add the frame to the frame table */
	fte->spte = spte;
	fte->clock_dirty = 1;
	list_push_back(&frame_table, &fte->elem);
	lock_release(&frame_table_lock);

	return fte->frame;
}

struct frame_table_entry* clock_eviction(){
	struct list_elem* clock_state_elem = list_begin(&frame_table);
	while(clock_state_elem != list_end(&frame_table)){
		struct frame_table_entry *fte = list_entry(clock_state_elem, struct frame_table_entry, elem);
		//if it's a dirty page on the first clock cycle, give it a 'second chance'
		if(!fte->spte->pin) {
		if(pagedir_is_dirty(fte->spte->owner->pagedir, fte->spte->upage) && fte->clock_dirty == 1){
			fte->clock_dirty = 0;
		}
		//if not dirty, select current page to be evicted if it hasn't been accessed
		else if(!pagedir_is_accessed(fte->spte->owner->pagedir, fte->spte->upage)){
			return fte;
		}
		//otherwise,set accessed bit to 0
		else{
			pagedir_set_accessed(fte->spte->owner->pagedir, fte->spte->upage,false);
		}
		}
		//circle back to the beginning of the list if we reach the end
		clock_state_elem = list_next(clock_state_elem);
		if(clock_state_elem == list_end(&frame_table)){
			clock_state_elem = list_begin(&frame_table);
		}
	}
}

void frame_evict (void){
    struct frame_table_entry *entry = clock_eviction();
    void* page = pagedir_get_page(entry->spte->owner->pagedir, entry->spte->upage);
    pagedir_clear_page(entry->spte->owner->pagedir, entry->spte->upage);
    int idx = swap_out(page);
    entry->spte->swap_table_idx = idx;
    entry->spte->status = INSWAP;

    list_remove(&entry->elem);
    entry->spte = NULL;
    list_push_back(&free_frames,&entry->elem);
}

void frame_free (void *frame){
	lock_acquire(&frame_table_lock);

	struct list_elem *e;
	for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
		struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
		if(fte->frame == frame){
			list_remove(e);
			fte->spte = NULL;
			list_push_back(&free_frames,&fte->elem);
			break;
		}
	}
	lock_release(&frame_table_lock);
}


