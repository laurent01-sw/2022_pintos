
struct mmap_entry
{
    struct list_elem l_elem;
    struct vm_entry *vme;   // Reuse all the info.
};