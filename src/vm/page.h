enum PAGE_TYPE 
{ 
    UNKNOWN     = 0x01, 
    ANONYMOUS   = 0x02, 
    FILE_BACKED = 0x04,
    ELF         = 0x08,
    MMAP        = 0x10,
    HUGE_PAGE   = 0x20
};


enum LOCATION
{
    NOWHERE     = 0x10,
    MEMORY      = 0x20,
    DISK        = 0x40,
    VALHALLA    = 0x80
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


// for swap management.
struct swap_info
{
    uint32_t loc;               // Location. Where?
    block_sector_t blk_idx;     // To where in disk?
};


struct mmap_info
{
    // struct list_elem l_elem;
    
    uint32_t loc;
    struct file *fobj;
    int32_t fd;

    int32_t ofs;        // Read offset

    size_t rbytes;      // page_read_bytes
    size_t zbytes;      // page_zero_bytes

    struct vm_entry *self;
};


// Somewhere defined.
struct pframe;
struct mmap_entry;

//
// Supplement Page Table
// : Enables page fault handling by supplementing the page table.
struct vm_entry
{
    // Project 3.
    struct hash_elem h_elem;    // Hash table element

    struct pframe *pf;          // Pframe element
    struct mmap_entry *me;      // mmap_entry element.

    uint8_t *vaddr;             // upage, virtual address
    uint8_t *paddr;             // Physical Address
    bool writable;              // Write permission
    bool hugepage;
    
    // For demand paging
    struct text_info ti;
    struct swap_info si;
    struct mmap_info mi;

    uint32_t page_type;         // ANONYM? FILE_BACKED?
};


//
// Hash init.
void init_vm_entry (
        struct vm_entry *, 
        uint8_t *,
        bool,
        struct text_info *,
        struct swap_info *,
        struct mmap_info *,
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
