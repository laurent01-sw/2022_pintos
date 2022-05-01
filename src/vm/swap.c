#include "lib/debug.h"
#include "lib/stddef.h"
#include "lib/kernel/bitmap.h"

#include "lib/kernel/list.h"
// #include "lib/kernel/hash.h"

#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "userprog/process.h"

#include "devices/block.h"
#include "userprog/pagedir.h"

#include "vm/page.h"
#include "vm/swap.h"
#include "vm/mmap.h"


struct block *swap_device;

extern struct list lru_list;
extern struct lock lru_list_lock;

extern struct bitmap *swap_bitmap;
extern struct lock swap_lock;

extern struct lock filesys_lock;


//
// Initialize bitmap and swap device
void
swap_init () 
{
    swap_device = block_get_role (BLOCK_SWAP);

    // printf ("swap_device: %p\n", swap_device);

    if (swap_device == NULL) return;
    // Case when not found.

    swap_bitmap = bitmap_create (block_size (swap_device));

    bitmap_set_all (swap_bitmap, true);
}


//
bool
swap_in (struct hash *vm, struct vm_entry *vme)
{
    
    void *kpage = alloc_pframe (PAL_USER | PAL_ZERO);
    int ofs = 0;
    
    if (kpage == NULL || vme->si.loc != DISK) 
        return false; // Unknown

    vme->paddr = kpage;

    if (!install_page (vme->vaddr, kpage, vme->writable))
        ASSERT (false);

    // Read
    

    if (vme->page_type == MMAP)
    {
        ASSERT (vme->mi.loc == DISK);

        if (!lock_held_by_current_thread (&filesys_lock))
            lock_acquire (&filesys_lock);

        file_seek (vme->mi.fobj, vme->mi.ofs);

        if (file_read (vme->mi.fobj, kpage, vme->mi.rbytes) != (int) vme->mi.rbytes)
        {
            palloc_free_page (kpage);
            ASSERT (false);
        }

        memset (kpage + vme->ti.rbytes, 0, vme->ti.zbytes);

        if (lock_held_by_current_thread (&filesys_lock))
            lock_release (&filesys_lock);
        
        vme->mi.loc = MEMORY;
    }
    else
    {
        lock_acquire (&swap_lock);
        for (ofs = 0; ofs < PGSIZE / BLOCK_SECTOR_SIZE; ofs++)
        {
            block_read (swap_device, vme->si.blk_idx + ofs, kpage);
            kpage += BLOCK_SECTOR_SIZE;
        }

        bitmap_set_multiple (swap_bitmap, vme->si.blk_idx, PGSIZE / BLOCK_SECTOR_SIZE, true);
        lock_release (&swap_lock);

        // Set location for swap_info
        vme->si.blk_idx = 0;
        vme->si.loc = MEMORY;

    }
    vme->pf->cnt = 0;


    lock_acquire (&lru_list_lock);
    list_insert_ordered (&lru_list, &(vme->pf->l_elem), access_less, NULL);
    lock_release (&lru_list_lock);

    return true;
}


//
bool
swap_out ()
{
    return swap_out_normal ();
}


bool
swap_out_normal ()
{
    int i = 0;
    void *ptr = NULL;

    lock_acquire (&lru_list_lock);
    if (list_empty (&lru_list)) 
    {
        lock_release (&lru_list_lock);
        lock_release (&swap_lock);

        return false;
    }

    // Selection among lru_list, what to evict
    struct list_elem *pframe_elem = list_pop_back (&lru_list);
    struct pframe *pf_ = list_entry (pframe_elem, struct pframe, l_elem);

    while (pf_->pinned || pf_->vme->si.loc == VALHALLA)
    {
        list_push_front (
            &(lru_list), &(pf_->l_elem)
        );
        pf_ = list_pop_back (&lru_list);
    } // Search for unpinned, and physically allocated page.

    lru_update ();
    
    lock_release (&lru_list_lock);
    /* Note that the lru_list is always ordered by its pframe->cnt number.
     */
    
    if (pf_->vme->page_type == MMAP)
    {
        /* When mapped file? */
        // Evict single page
        // if (!lock_held_by_current_thread (&filesys_lock))
        lock_acquire (&filesys_lock);

        file_seek (pf_->vme->mi.fobj, pf_->vme->mi.ofs);
        file_write (
            pf_->vme->mi.fobj, 
            pf_->vme->vaddr, 
            pf_->vme->mi.rbytes);

        // if (lock_held_by_current_thread (&filesys_lock))
        lock_release (&filesys_lock);

        pf_->vme->mi.loc = DISK;
    }
    else 
    {
        lock_acquire (&swap_lock);
        size_t idx = bitmap_scan_and_flip (swap_bitmap, 0, PGSIZE / BLOCK_SECTOR_SIZE, true); 
        
        if (idx == BITMAP_ERROR)
            ASSERT (false);

        // Set starting point (for swap)
        ptr = pf_->vme->paddr;
        
        // If Stack or ELF,
        for (i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
        {
            block_write (swap_device, idx + i, ptr);
            ptr += BLOCK_SECTOR_SIZE;
        }

        
        pf_->vme->si.loc = DISK;
        pf_->vme->si.blk_idx = idx;

        lock_release (&swap_lock);
    }
    
    pagedir_clear_page (thread_current ()->pagedir, pf_->vme->vaddr);   // Unregister
    palloc_free_page (pf_->vme->paddr);                                 // Free the target !!!!

    pf_->vme->paddr = NULL;
    pf_->cnt = 0;

    return true;
}


bool
flush_mmap (uint32_t map_id) // Flushes out all the pages.
{
    struct thread *cur = thread_current ();
    struct list_elem *e, *next_e;
    struct mmap_entry *me;
    void *paddr;

    int fd_idx = cur->fd[map_id];

    /* Validation Check */
    if (cur->mmap_file[fd_idx] == false)
        return false;

    for (e = list_begin (&(cur->mmap_pages)); 
            e != list_end (&(cur->mmap_pages)); )
    {
        me = list_entry (e, struct mmap_entry, l_elem);       

        if (me->vme->mi.fobj == cur->fd_file[fd_idx])
        {
            if (pagedir_is_dirty (cur->pagedir, me->vme->vaddr))
            {
                // if (!lock_held_by_current_thread (&filesys_lock))
                lock_acquire (&filesys_lock); //printf ("content: %s\n", me->vme->vaddr);

                file_allow_write (me->vme->mi.fobj);

                // file_seek (me->vme->mi.fobj, me->vme->mi.ofs);
                file_seek (me->vme->mi.fobj, 0);
                if (file_write_at (
                    me->vme->mi.fobj, 
                    me->vme->vaddr, 
                    me->vme->mi.rbytes,
                    me->vme->mi.ofs)
                        != (int)(me->vme->mi.rbytes)
                    )
                        return (false);
                
                // if (lock_held_by_current_thread (&filesys_lock))
                lock_release (&filesys_lock);
            }

            next_e = list_next (e);
            list_remove (e);
            
            pagedir_clear_page (cur->pagedir, me->vme->vaddr);

            paddr = me->vme->paddr;         // Physical page?
            palloc_free_page (paddr);       // Delete the physical page

            delete_vme (&(cur->vm), me->vme);
            e = next_e;
        }
        else 
        {
            e = list_next (e);
        }        
    }
    /* Clear the file. */
    cur->mmap_file[fd_idx] = false;     // Set the index to false,

    return true;
}


//
void *
alloc_pframe (enum palloc_flags flags)
{
    void *kpage = palloc_get_page (flags);
    struct thread *t = thread_current ();

    if (kpage == NULL)  // Page unavailable
    {
        // idx = bitmap_scan_and_flip (swap_bitmap, 0, PGSIZE / BLOCK_SECTOR_SIZE, false); // Search for free slots
        if (!swap_out ())
            ASSERT (false);

        // One more try
        kpage = palloc_get_page (flags);
        ASSERT (kpage != NULL);
    }

    return kpage;
}



bool
access_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct pframe *a = list_entry (a_, struct pframe, l_elem);
  const struct pframe *b = list_entry (b_, struct pframe, l_elem);
  
  return a->cnt < b->cnt;
}

//
void
lru_update ()
{   
    struct thread *t = thread_current ();
    struct list_elem *e;
    // int i;

    struct hash_iterator i;

    hash_first (&i, &(t->vm));
    while (hash_next (&i))
    {
        struct vm_entry *vme = hash_entry (hash_cur (&i), struct vm_entry, h_elem);
        
        if (pagedir_is_accessed (t->pagedir, vme->vaddr))
            vme->pf->cnt += 1;
    }

    list_sort (&lru_list, access_less, NULL);
}



int
register_mmap (int fd, void *upage)
{
    // Demand paging scheme
    struct thread *cur = thread_current ();
    int file_size = -1;

    struct vm_entry *vme;

    /* 1. Search file info. */
    int pos = 0;            // Internal index.
    for (pos = 0; pos < cur->fd_pos; pos++)
    {
        if (cur->fd[pos] == fd) // found?
        {
            file_size = file_length (cur->fd_file[pos]); // Get the file size.
            break;
        }
    }

    if (file_size < 0)
        return -1;
    
    // use the same position index.
    cur->mmap_file[pos] = true;

    ASSERT (fd != 0);
    ASSERT (fd != 1);
    
    /* 2. Allocate the vm_entry */
    struct text_info text_info_ = {
        .owner      = cur,
        .exe_file   = 0,
        .rbytes     = 0,
        .zbytes     = 0,
    };    

    struct swap_info swap_info_ = {
        .loc        = VALHALLA,
        .blk_idx    = 3
    };

    struct mmap_info mmap_info_ = {
        .loc        = VALHALLA,
        .fobj       = cur->fd_file[pos],
        .fd         = pos,
        .ofs        = 0,
        .rbytes     = 0,
        .zbytes     = 0,
    };

    upage = pg_round_down (upage);

    while (file_size > 0) 
    {
        
        mmap_info_.rbytes = file_size < PGSIZE ? file_size          : PGSIZE;
        mmap_info_.zbytes = file_size < PGSIZE ? PGSIZE - file_size : PGSIZE;
       
        if ((vme = find_vme (&(cur->vm), upage)) != NULL)
        {
            // printf ("Registering: Address found\n");
            return -1;
        }
        else
        {
            vme = malloc (sizeof (struct vm_entry));           
            vme->paddr = NULL;

            init_vm_entry (
                vme,    // vm_entry
                upage,  // vaddr?
                true,   // Writable?
                &text_info_,
                &swap_info_,
                &mmap_info_,
                MMAP
            );

            vme->me = malloc (sizeof (struct mmap_entry));
            vme->me->vme = vme; // Points to self.

            mmap_info_.self = vme;

            if (!insert_vme ( &(cur->vm), vme))
            {
                free (vme);
                ASSERT (false); // Raise panic.
            }

            // debug_vm_entry (vme);
            list_push_back (&(cur->mmap_pages), vme->me);
        }

        mmap_info_.ofs  += PGSIZE;
        file_size       -= PGSIZE;
        upage           += PGSIZE;

        // debug_vm_entry (vme);
    }

    return cur->fd[pos];
}


