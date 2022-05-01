#include "lib/debug.h"
#include "lib/stddef.h"

// #include "lib/kernel/list.h"
#include "lib/kernel/bitmap.h"
#include "lib/kernel/hash.h"

#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "devices/block.h"

#include "vm/page.h"
#include "vm/swap.h"
#include "vm/mmap.h"

/* Returns a hash value for page p. */
unsigned
vm_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct vm_entry *p = hash_entry(p_, struct vm_entry, h_elem);
    return hash_bytes(&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool vm_less(const struct hash_elem *a_, const struct hash_elem *b_,
             void *aux UNUSED)
{
    const struct vm_entry *a = hash_entry(a_, struct vm_entry, h_elem);
    const struct vm_entry *b = hash_entry(b_, struct vm_entry, h_elem);

    return a->vaddr < b->vaddr;
}

// Follows: hash_clear.
void vm_destroy(struct hash *vm)
{
    size_t i;

    for (i = 0; i < vm->bucket_cnt; i++)
    {
        struct list *bucket = &vm->buckets[i];
        while (!list_empty(bucket))
        {
            struct list_elem *list_elem = list_pop_front(bucket);
            struct hash_elem *hash_elem = list_elem_to_hash_elem(list_elem);

            delete_vme(
                vm,
                hash_entry(hash_elem, struct vm_entry, h_elem));

            hash_elem = hash_delete(vm, hash_elem);
        }

        list_init(bucket);
    }

    vm->elem_cnt = 0;
}

extern struct list lru_list;
extern struct lock lru_list_lock;

extern struct lock swap_lock;
extern struct block *swap_device;

// Initialize VM struct.
void init_vm_entry (
    struct vm_entry *vme,    // What to modify?
    uint8_t *vaddr,          // Allocated User Page
    bool writable,           // Write permission?
    struct text_info *tinfo, // ELF information
    struct swap_info *sinfo, // Swap information
    struct mmap_info *minfo,
    uint32_t page_type)
{
    ASSERT(vme != NULL);
    // printf ("init_vm_entry\n");

    vme->vaddr          = vaddr;
    vme->writable       = writable;

    vme->pf             = malloc (sizeof (struct pframe));
    vme->pf->vme        = vme;
    vme->pf->cnt        = 0;

    vme->pf->pinned     = false;

    vme->me             = NULL; // mmap is fixed to NULL at first.

    lock_acquire (&lru_list_lock);
    list_insert_ordered (&lru_list, &(vme->pf->l_elem), access_less, NULL);
    lock_release (&lru_list_lock);

    // vme->ti.exe_name    = tinfo->exe_name;
    vme->ti.owner       = tinfo->owner;
    vme->ti.exe_file    = tinfo->exe_file;
    vme->ti.ofs         = tinfo->ofs;
    vme->ti.rbytes      = tinfo->rbytes;
    vme->ti.zbytes      = tinfo->zbytes;

    vme->si.loc         = sinfo->loc;
    vme->si.blk_idx     = sinfo->blk_idx;

    vme->page_type      = page_type;

    vme->mi.loc         = minfo->loc;
    vme->mi.fobj        = minfo->fobj;
    vme->mi.ofs         = minfo->ofs;
    vme->mi.rbytes      = minfo->rbytes;
    vme->mi.zbytes      = minfo->zbytes;

    return;
}

void debug_vm_entry(struct vm_entry *vme)
{
    printf ("  <vme info: %p>\n", vme);

    printf ("  - vme->pf: %p\n", vme->pf);
    printf ("  - vme->me: %p\n", vme->me);

    printf ("   --------\n");
    printf ("  - vme->page_type: %p\n", vme->page_type);

    printf ("  - vme->vaddr: %p\n", vme->vaddr);
    printf ("  - vme->paddr: %p\n", vme->paddr);
    printf ("  - vme->writable: %d\n", vme->writable);

    printf ("   --------\n");
    printf ("  - vme->ti.owner->name: %s\n", vme->ti.owner->name);
    printf ("  - vme->ti.ofs: %d\n", vme->ti.ofs);
    printf ("  - vme->ti.rbytes: %d\n", vme->ti.rbytes);
    printf ("  - vme->ti.zbytes: %d\n", vme->ti.zbytes);

    printf ("   --------\n");
    
    switch (vme->si.loc)
    {
        case NOWHERE:   printf ("  - vme->si.loc: NOWHERE\n");    break;
        case MEMORY:    printf ("  - vme->si.loc: MEMORY\n");     break;
        case DISK:      printf ("  - vme->si.loc: DISK\n");       break;
        case VALHALLA:  printf ("  - vme->si.loc: VALHALLA\n");   break;
    }
    printf ("  - vme->si.blk_idx: %d\n", vme->si.blk_idx);

    printf ("   --------\n");
    switch (vme->mi.loc)
    {
        case NOWHERE:   printf ("  - vme->mi.loc: NOWHERE\n");    break;
        case MEMORY:    printf ("  - vme->mi.loc: MEMORY\n");     break;
        case DISK:      printf ("  - vme->mi.loc: DISK\n");       break;
        case VALHALLA:  printf ("  - vme->mi.loc: VALHALLA\n");   break;
    }
    printf ("  - vme->mi.fobj: %p\n", vme->mi.fobj);

    printf ("  - vme->mi.ofs: %d\n", vme->mi.ofs);
    printf ("  - vme->mi.rbytes: %d\n", vme->mi.rbytes);
    printf ("  - vme->mi.zbytes: %d\n", vme->mi.zbytes);


    if (vme->pf != NULL)
    {
        ;
    }

}

/* Inserts VM entry.
 */
bool insert_vme (struct hash *vm, struct vm_entry *vme)
{
    struct hash_elem *success = hash_insert(vm, &(vme->h_elem));
    return success == NULL;
}


/* Searches for a vm_entry.
 */
struct vm_entry *
find_vme (struct hash *vm, void *vaddr)
{
    // 1st arg: Which vm table to search?
    // 2nd arg: What is the target?
    struct vm_entry t_vme; // What to find
    t_vme.vaddr = vaddr;

    struct hash_elem *e = hash_find (vm, &t_vme.h_elem);

    return e != NULL ? hash_entry (e, struct vm_entry, h_elem) : NULL;
}


// Deletes vm_entry, 'and' associated pframe.
bool 
delete_vme (struct hash *vm, struct vm_entry *vme)
{
    struct hash_elem *e = hash_delete (vm, &(vme->h_elem));
    
    if (e == NULL) // Case when no element is found.
        return true;

    struct vm_entry *fvme = hash_entry (e, struct vm_entry, h_elem);

    ASSERT(fvme == vme);

    lock_acquire (&lru_list_lock);
    list_remove (&(vme->pf->l_elem));   // 1. Remove from the lru
    lock_release (&lru_list_lock);

    if (vme->page_type != MMAP && vme->si.loc == DISK)
    {
        lock_acquire (&swap_lock);
        bitmap_set_multiple (swap_device, vme->si.blk_idx, PGSIZE / BLOCK_SECTOR_SIZE, true);
        lock_release (&swap_lock);
    }
    else
    {
        ;
    }

    free (vme->pf);
    
    if (vme->me != NULL)
        free (vme->me);

    // if (vme->mi.fobj != NULL)
    //     file_close (vme->mi.fobj);

    free (vme);
    return true;
}
