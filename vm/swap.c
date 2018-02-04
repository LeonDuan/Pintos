#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include "vm/swap.h"

void init_swap_table (void){
	lock_init(&swap_lock);

	/* get swap device */
	swap_device = block_get_role (BLOCK_SWAP);
	if (swap_device == NULL) PANIC("Cannot initialize swap device");

	/* init swap table */
	swap_table = bitmap_create (block_size (swap_device) / SECTORS_PER_PAGE);
	if (swap_table == NULL) PANIC ("Cannot create swap table");

	/* all swap slots available */
	bitmap_set_all (swap_table, SLOT_FREE);
}

/* swap a page out of VM into swap space*/
int swap_out (void * frame){
	if (!swap_device || !swap_table){
		return -1;
	}
	lock_acquire(&swap_lock);

	/* get a free slot from swap table (and flip the corresponding bit */
	int free_slot_idx = bitmap_scan_and_flip(swap_table, 0, 1, SLOT_FREE);

	/* check for error */
	if (free_slot_idx == BITMAP_ERROR) PANIC("Swap partition is full!");

	/* write the frame */
	int i;
	for(i = 0; i < SECTORS_PER_PAGE; i++){
	      block_write (swap_device, free_slot_idx * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * BLOCK_SECTOR_SIZE);
	}

	lock_release(&swap_lock);

	return free_slot_idx;
}

/* swap a page into VM from swap space*/
void swap_in (int idx, void *frame){
	if (!swap_device || !swap_table){
		return;
	}
	lock_acquire(&swap_lock);

	/* read the content back to VM*/
	int i;
	for(i = 0; i < SECTORS_PER_PAGE; i++){
		block_read (swap_device, idx * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * BLOCK_SECTOR_SIZE);
	}

	/* mark the slot free */
	bitmap_flip(swap_table, idx);

	lock_release(&swap_lock);
}

/* clears the swap slot */
void swap_clear (int idx){
	bitmap_set (swap_table, idx, true);
}
