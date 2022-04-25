enum PAGE_TYPE { 
    UNKNOWN     = 0x01, 
    ANONYMOUS   = 0x02, 
    FILE_BACKED = 0x04,
    ELF         = 0x08,
};

// For ELF demand paging
struct text_info
{
    /* Where did this page come from? 
     * Only when text seg.
     * Refer to load_segment() 
     */

    struct thread *owner;
    struct file* exe_file;  // From what file?
    int32_t ofs;        // Read offset

    size_t rbytes;      // page_read_bytes
    size_t zbytes;      // page_zero_bytes
};


//
// 1. Supplement Page Table
// : Enables page fault handling by supplementing the page table.
struct vm_entry
{
    // Project 3.
    struct hash_elem elem;  // Hash table element

    uint8_t *vaddr;         // upage, virtual address
    uint8_t *paddr;         // Physical Address
    bool writable;          // Write permission
    
    // For demand paging
    struct text_info ti;

    uint32_t page_type;     // ANONYM? FILE_BACKED?
};


//
// Hash init.
void init_vm_entry (
        struct vm_entry *, 
        uint8_t *,
        bool,
        struct text_info *,
        uint32_t PAGE_TYPE
        );

// Hash generating functions and less_func.
unsigned vm_hash (const struct hash_elem *, void* UNUSED);
bool vm_less (const struct hash_elem *, const struct hash_elem *, void * UNUSED);

void vm_destructor (struct hash_elem *e, void *aux UNUSED);
void vm_destroy(struct hash *vm);

// Hash Table Controllers,
bool insert_vme (struct hash *, struct vm_entry *);
struct vm_entry * find_vme (struct hash *, void *);
bool delete_vme (struct hash *vm, struct vm_entry *vme);


void debug_vm_entry (struct vm_entry *vme);


//
// 2. Physical Page Representation
struct page
{
    struct list_elem elem;  // List element
    
    struct vm_entry *vme;
};

