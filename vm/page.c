#include <stdlib.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <stdbool.h>


static bool install_page(void* upage, void* kpage, bool writable);

void page_table_init(struct hash *spt){
	hash_init (spt, spte_hash_func, spte_less_func, NULL);
}

void page_table_destroy (struct hash *spt) {
	hash_destroy (spt, spte_destroy_func);
}

bool page_add (struct hash *spt, void *upage, int status, 
	struct file *file, off_t ofs, uint32_t read_bytes,
	uint32_t zero_bytes, bool writable) {
	struct supp_page_table_entry *spte = (struct supp_page_table_entry *) malloc(sizeof(struct supp_page_table_entry));
	spte->owner = thread_current();
	spte->upage = upage;
	spte->swap_table_idx = -1;
	spte->status = status;
	spte->writable = writable;
	spte->pin = false;
	lock_init(&spte->load_lock);

	spte->file = file;
	spte->ofs = ofs;
	spte->read_bytes = read_bytes;
	spte->zero_bytes = zero_bytes;

	volatile bool result = (hash_insert(spt, &spte->elem) == NULL);
	if (!result){
	    free (spte);
	}
	return result;
}

bool page_map_to_frame(void* addr, void* sp, bool unpin){
    if(addr >= sp - STACK_THRESH
            && addr < PHYS_BASE && addr >= PHYS_BASE - MAX_STACK_SIZE) {
        if(grow_stack(&thread_current()->supp_page_table,addr,unpin))
            return true;
    } else {
        struct supp_page_table_entry* spte = page_find(&thread_current()->supp_page_table,addr);
        if (spte != NULL) {
            if(load_page (spte)) {
                if (unpin) page_unpin(&thread_current()->supp_page_table,addr);
                return true;
            }
        }
    }
    return false;
}

bool load_page (struct supp_page_table_entry *spte){
	lock_acquire(&spte->load_lock);
	spte->pin = true;
	if(spte->status == INFRAME) {
		lock_release(&spte->load_lock);
		return true;
	}
	void *kpage = frame_alloc(spte);
	if(kpage == NULL) {
		spte->pin = false;
		lock_release(&spte->load_lock);
		return false;
	}
	if(spte->status == INFILE) {
		/* Load this page. */
		file_seek(spte->file,spte->ofs);
		if (file_read (spte->file, kpage, spte->read_bytes) != (int) spte->read_bytes) {
			frame_free (kpage);
			lock_release(&spte->load_lock);
			return false;
		}
		memset (kpage + spte->read_bytes, 0, spte->zero_bytes);
	} else if (spte->status == INSTACK) {
		memset (kpage, 0, spte->zero_bytes);
	} else if (spte->status == INSWAP) {
		swap_in(spte->swap_table_idx, kpage);
		spte->swap_table_idx = -1;
	}
	
	/* Add the page to the process's address space. */
	if (!install_page (spte->upage, kpage, spte->writable)) {
		frame_free (kpage);
		spte->pin = false;
		lock_release(&spte->load_lock);
		return false;
	}
	spte->status = INFRAME;
	lock_release(&spte->load_lock);
	return true;
}

bool grow_stack (struct hash *spt, void *va, bool unpin){
	void* page_boundary = pg_round_down(va);
	page_add(spt, page_boundary, INSTACK, NULL, 0, 0, PGSIZE, true);
	struct supp_page_table_entry *spte = page_find(spt,page_boundary);
	if(spte == NULL) return false;
	bool ret = load_page(spte);
	if (unpin) page_unpin(spt, va);
	return ret;
}

struct supp_page_table_entry *page_find(struct hash *spt, void *va) {
	void* page_boundary = pg_round_down(va);
	struct supp_page_table_entry spte;
	spte.upage = page_boundary;
	struct hash_elem *elem = hash_find(spt,&spte.elem);
	if(elem == NULL) return NULL;
	return hash_entry(elem, struct supp_page_table_entry, elem);
}

void page_unpin(struct hash *spt, void *upage) {
	struct supp_page_table_entry *spte = page_find(spt, upage);
	lock_acquire(&spte->load_lock);
	if(spte->status == INFRAME)
		spte->pin = false;
	lock_release(&spte->load_lock);
}

/* Hash Functions */
unsigned spte_hash_func(const struct hash_elem *elem, void *aux UNUSED) {
  struct supp_page_table_entry *entry = hash_entry(elem, struct supp_page_table_entry, elem);
  return hash_int( (int)entry->upage );
}

bool spte_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  struct supp_page_table_entry *a_entry = hash_entry(a, struct supp_page_table_entry, elem);
  struct supp_page_table_entry *b_entry = hash_entry(b, struct supp_page_table_entry, elem);
  return a_entry->upage < b_entry->upage;
}

void spte_destroy_func(struct hash_elem *elem, void *aux UNUSED) {
  struct supp_page_table_entry *spte = hash_entry(elem, struct supp_page_table_entry, elem);
  if (spte->status == INFRAME){
	  frame_free(pagedir_get_page(thread_current()->pagedir, spte->upage));
	  pagedir_clear_page(thread_current()->pagedir, spte->upage);
  }
  else if (spte->status == INSWAP){
	  swap_clear(spte->swap_table_idx);
  }

  free(spte);
}

bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

