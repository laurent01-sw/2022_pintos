// Interfaces

struct pframe
{
    struct list_elem l_elem;

    struct vm_entry *vme;   // Reuse all the info.
    uint32_t cnt;

    bool pinned;
};



void swap_init ();

bool swap_in (struct hash *, struct vm_entry *);

bool swap_out ();
bool swap_out_normal ();
bool flush_mmap (uint32_t);

void * alloc_pframe (enum palloc_flags);
void lru_update ();

bool access_less (
    const struct list_elem *, 
    const struct list_elem *,
            void *) ;
