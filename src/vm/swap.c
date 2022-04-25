#include "lib/debug.h"
#include "lib/stddef.h"
#include "lib/kernel/bitmap.h"

#include "lib/kernel/list.h"
// #include "lib/kernel/hash.h"

#include "threads/palloc.h"
#include "threads/thread.h"

#include "devices/block.h"

#include "vm/page.h"
#include "vm/swap.h"

struct block *swap_device;

extern struct bitmap *bm_swap;
extern struct lock bm_swap_lock;

//
// Initialize bitmap and swap device
void
swap_init () 
{
    swap_device = block_get_role (BLOCK_SWAP);

    if (swap_device == NULL) return;
    // Case when not found.
    
    bm_swap = bitmap_create (block_size (swap_device));

    bitmap_set_all (bm_swap, false);
}


//
bool
swap_in (struct vm_entry *vme, void *page)
{

    return true;
}


//
bool
swap_out (struct vm_entry *vme, void *page)
{

    return true;
}



//
void
lru_insert ()
{

}



void
lru_remove ()
{

}






