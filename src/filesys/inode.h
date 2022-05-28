#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define BH_ENTRY 64
#define BH_MAGIC 0xfaceb00c

struct bitmap;

/* Buffer Cache Data Structure */
enum bh_state_bits {
	BH_Used,
        BH_Dirty,       
};

struct buffer_head
  {
    uint32_t b_state;          
    uint32_t b_magic;
    uint32_t *b_page;            	/* the page this bh is mapped to */
    char *b_start_page;			/* Only a quarter of a page is used by buffer head 
					   This field points to the used portion of the page 
					   by this buffer_head */

    block_sector_t b_blocknr;           /* start block number */
    struct list_elem elem;		/* All Buffer_head is enumerated at b_list */
    /* struct list bh_lock; 		Lock need to be added after filesys_lock is removed */ 
    off_t pos;				/* Offeset in b_page; b_page can accomodate 4 buffer_head */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
bool inode_isdir (const struct inode *);
void inode_setdir (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
struct buffer_head *sector_to_bhead (block_sector_t, bool);
struct buffer_head *evict_bcache_entry (void);
struct buffer_head *find_bcache_entry (block_sector_t);
void pdflush (void);

#endif /* filesys/inode.h */
