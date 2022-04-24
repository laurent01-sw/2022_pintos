#include "lib/debug.h"
#include "lib/stddef.h"

// #include "lib/kernel/list.h"
#include "lib/kernel/hash.h"

#include "threads/palloc.h"
#include "threads/thread.h"

#include "vm/page.h"

/* Returns a hash value for page p. */
unsigned
vm_hash (const struct hash_elem *p_, void *aux UNUSED)
{
    const struct vm_entry *p = hash_entry (p_, struct vm_entry, elem);
    return hash_bytes (&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool
vm_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
    const struct vm_entry *a = hash_entry (a_, struct vm_entry, elem);
    const struct vm_entry *b = hash_entry (b_, struct vm_entry, elem);

    return a->vaddr < b->vaddr;
}

// void
// vm_destructor (struct hash_elem *e, void *aux UNUSED)
// {
//     struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);

//     ASSERT (vme != NULL);
//     free (vme);
// }

// Follows: hash_clear.
void vm_destroy (struct hash *vm)
{
    size_t i;

    for (i = 0; i < vm->bucket_cnt; i++) 
    {
        struct list *bucket = &vm->buckets[i];

        while (!list_empty (bucket)) 
        {
            struct list_elem *list_elem = list_pop_front (bucket);
            struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);

            delete_vme (
                vm, 
                hash_entry (hash_elem, struct vm_entry, elem));

            hash_elem = hash_delete (vm, hash_elem);
        }

    list_init (bucket); 
    }    

  vm->elem_cnt = 0;
}


// Initialize VM struct.
void
init_vm_entry (
        struct vm_entry *vme,       // What to modify?
        uint8_t *vaddr,             // Allocated User Page
        bool writable,              // Write permission?
        struct text_info *tinfo,    // ELF information
        enum PAGE_TYPE page_type)
{
    ASSERT (vme != NULL);

    vme->vaddr      = vaddr;
    vme->writable   = writable;

    // vme->ti.exe_name    = tinfo->exe_name;
    vme->ti.owner       = tinfo->owner;
    vme->ti.exe_file    = tinfo->exe_file;
    vme->ti.ofs         = tinfo->ofs;
    vme->ti.rbytes      = tinfo->rbytes;
    vme->ti.zbytes      = tinfo->zbytes;

    vme->page_type = page_type;

    return;
}

void
debug_vm_entry (struct vm_entry *vme)
{
    printf ("  <vme info: %p>\n", vme);

    printf ("  - vme->vaddr: %p\n", vme->vaddr);
    printf ("  - vme->writable: %d\n", vme->writable);
    // printf ("  vme->ti.exe_name: %s\n", vme->ti.exe_name);
    printf ("  - vme->ti.owner->name: %s\n", vme->ti.owner->name);

    printf ("  - vme->ti.ofs: %d\n", vme->ti.ofs);
    printf ("  - vme->ti.rbytes: %d\n", vme->ti.rbytes);
    printf ("  - vme->ti.zbytes: %d\n", vme->ti.zbytes);
}

/* Inserts VM entry.
 */
bool
insert_vme (struct hash *vm, struct vm_entry *vme) 
{
    struct hash_elem *success = hash_insert (vm, &(vme->elem)); 

    return success == NULL;
}

/* Searches for a vm_entry.
 */
struct vm_entry* 
find_vme (struct hash *vm, void *vaddr)
{
    // 1st arg: Which vm table to search?
    // 2nd arg: What is the target?
    struct vm_entry t_vme; // What to find
    t_vme.vaddr = vaddr;

    struct hash_elem *e = hash_find (vm, &t_vme.elem);

    return e != NULL ? hash_entry (e, struct vm_entry, elem) : NULL;
}

bool
delete_vme (struct hash *vm, struct vm_entry *vme)
{
    struct hash_elem *e = hash_delete (vm, &(vme->elem));

    if (e == NULL) // Case when no element is found.
        return true;

    struct vm_entry *fvme = hash_entry (e, struct vm_entry, elem);

    ASSERT (fvme == vme);

    free (vme);
    return true;
}

