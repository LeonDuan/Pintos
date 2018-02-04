#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include <stdbool.h>

#define SECTORS_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)
#define SLOT_FREE 0
#define SLOT_OCCUPIED 1

struct lock swap_lock;
struct block *swap_device;
struct bitmap *swap_table; /* the swap table */

void init_swap_table (void);
int swap_out (void *frame);
void swap_in (int idx, void *frame);
void swap_clear (int idx);

#endif /* vm/swap.h */
