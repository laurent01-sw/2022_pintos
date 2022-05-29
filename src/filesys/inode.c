#include "filesys/inode.h"
#include <stdio.h>
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES (DIRECT_BLOCK_ENTRIES + 128)
#define DINDIRECT_BLOCK_ENTRIES (INDIRECT_BLOCK_ENTRIES + (128 * 128))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    unsigned isdir;
    /* Extensible File Support */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
    block_sector_t indirect_block_sec;
    block_sector_t double_indirect_block_sec;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

struct list bh_list;
struct lock bh_list_lock; /* Lock need to be acquired after filesys_lock is removed */
static int active_b_count; /* Count How many buffer cache entry is used curretly.
			      Protected by bh_list_lock */
 
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  struct buffer_head *b_head = NULL;
  block_sector_t sector_pos;
  uint32_t *page_pos = NULL;

  ASSERT (inode != NULL);

  sector_pos = pos / BLOCK_SECTOR_SIZE;

  if (pos < inode->data.length)
    {
      if (sector_pos < DIRECT_BLOCK_ENTRIES)
	// Read from Direct Pointer 
        {
	   return inode->data.direct_map_table[sector_pos];
	}
      else if (sector_pos < INDIRECT_BLOCK_ENTRIES)
	// Read from Single Indirect Pointer 
	{
	  // Read Indirect Pointer Block 
	  //printf ("(indirect) sector_pos : %d\n", sector_pos);
	  sector_pos -= DIRECT_BLOCK_ENTRIES;
	  b_head = find_bcache_entry (inode->data.indirect_block_sec);	
	  page_pos = (uint32_t *) b_head->b_start_page;
	  page_pos = page_pos + sector_pos;
	  return *page_pos;
	}
      else if (sector_pos < DINDIRECT_BLOCK_ENTRIES)
	// Read from Double Indirect Pointer 
	{
	  //printf ("(double_indirect) sector_pos : %d\n", sector_pos);
	  // Read Double Indirect Pointer Block
	  sector_pos -= INDIRECT_BLOCK_ENTRIES;
	  b_head = find_bcache_entry (inode->data.double_indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  page_pos = page_pos + (sector_pos / 128);
	  // Read Indirect Pointer Block
	  b_head = find_bcache_entry (*page_pos);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  page_pos = page_pos + (sector_pos % 128);
	  return *page_pos;
	}
      else
        {
	  ASSERT(false);
	}
      // return inode->data.start + pos / BLOCK_SECTOR_SIZE;
    }
  else
    return -1;
  
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
/* Extensible File Support */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool i_pblock = true, di_pblock = true, d_block = true, i_block = true, di_block = true;
  uint32_t *page_pos, *ipage_pos, p_count, f_count, s_count = 0, i, j;
  block_sector_t sector_len, d_sector_num;
  struct buffer_head *b_head, *ib_head;
  static char zeros [BLOCK_SECTOR_SIZE];

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      
      sector_len = sectors;
  
      //printf ("s_length : %d, SI : %d, DI : %d\n"
	//	      , sectors, sectors > DIRECT_BLOCK_ENTRIES
	//	      , sectors > INDIRECT_BLOCK_ENTRIES);
      
      /* Panic if Out of Maximum File Size*/
      ASSERT (sector_len <= DINDIRECT_BLOCK_ENTRIES)

      /* initialize pointer block */
      if (sector_len > DIRECT_BLOCK_ENTRIES)
        {
	  if (!free_map_allocate (1, &disk_inode->indirect_block_sec))
	    goto done;
	  else
	    block_write (fs_device, disk_inode->indirect_block_sec, zeros);
	}
      if (sector_len > INDIRECT_BLOCK_ENTRIES)
        {
	  if (!free_map_allocate (1, &disk_inode->double_indirect_block_sec))
	    {
	      i_pblock = false;
	      goto done; /* Clean inode indirect Block */
	    }
	  else
	    block_write(fs_device, disk_inode->double_indirect_block_sec, zeros);

	  d_sector_num = sector_len - INDIRECT_BLOCK_ENTRIES;
	  b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  for (p_count = 0; p_count < DIV_ROUND_UP(d_sector_num, 128); p_count++)
	    {
	      free_map_allocate (1, page_pos + p_count);
	      block_write (fs_device, *(page_pos + p_count), zeros);
	    }
	  b_head->b_state |= (1UL << BH_Dirty);
	  if (p_count != DIV_ROUND_UP(d_sector_num, 128)) 
	    /* Fail to allocate whole dindirect block set */
	    {
	      di_pblock = i_pblock = false;
	      goto done;
	    }
	}

      b_head = ib_head = NULL;
      while (sectors > 0)
	{
          if (s_count < DIRECT_BLOCK_ENTRIES)
	      // Store to Direct Pointer 
            {
	      if (!free_map_allocate (1, &disk_inode->direct_map_table[s_count]))
	        {
		  d_block = di_pblock = i_pblock = false;
	          break;
		}
	      else
	        block_write(fs_device, disk_inode->direct_map_table[s_count], zeros);
	    }
          else if (s_count < INDIRECT_BLOCK_ENTRIES)
	      // Store to Single Indirect Pointer 
	    { 
	      // Read Indirect Pointer Block 
	      if (!b_head)
	      { 
	        b_head = find_bcache_entry (disk_inode->indirect_block_sec);	
	        page_pos = (uint32_t *) b_head->b_start_page;
	      }

	      if (!free_map_allocate (1, page_pos + (s_count - DIRECT_BLOCK_ENTRIES)))
	        {
		  i_block = d_block = di_pblock = i_pblock = false;
	          break;
		}
	      else
	        block_write(fs_device, *(page_pos + s_count - DIRECT_BLOCK_ENTRIES), zeros);

	      if (s_count == INDIRECT_BLOCK_ENTRIES - 1)
	        b_head = NULL;
	      
	      b_head->b_state |= (1UL << BH_Dirty);
	    }
          else if (s_count < DINDIRECT_BLOCK_ENTRIES)
	      // Read from Double Indirect Pointer 
	    {
	      // Read Double Indirect Pointer Block
	      if (!b_head)
	        {
		  b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	          page_pos = (uint32_t *) b_head->b_start_page;
		}
	      // Read Indirect Pointer Block
	      if (!ib_head)
	        {
		  ib_head = find_bcache_entry (*(page_pos 
					  + (s_count - INDIRECT_BLOCK_ENTRIES)/ 128));
	          ipage_pos = (uint32_t *) ib_head->b_start_page;
	      	  ipage_pos = ipage_pos + (s_count - INDIRECT_BLOCK_ENTRIES) % 128;
		}
	      ib_head = s_count - INDIRECT_BLOCK_ENTRIES % 128 == 127 ? NULL : ib_head;
	      
	      if (!free_map_allocate (1, ipage_pos + (s_count - DIRECT_BLOCK_ENTRIES)))
	        {
		  di_block = i_block = d_block = di_pblock = i_pblock = false;
	          break;
		}
	      else
	        block_write(fs_device, *(ipage_pos + s_count - DIRECT_BLOCK_ENTRIES), zeros);
	  
	      ib_head->b_state |= (1UL << BH_Dirty);
	    }
          else
            {
	      ASSERT(false);
	    }
	  sectors--; s_count++;
	}
done:
      if (!di_block) /* Clean Data Block Pointed by Dindirect Pointer Block */
	{
	  b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  for (i = 0; *(page_pos + i) != 0; j++)
	    {
	      ib_head = find_bcache_entry (*(page_pos + i));
	      ipage_pos = (uint32_t *) ib_head->b_start_page;
	      for (j = 0; *(ipage_pos + j) != 0 ; j++)	
		free_map_release (*(ipage_pos + j), 1);	
	      memset (ib_head->b_start_page, 0, 512); 
	    }
	  memset (b_head->b_start_page, 0, 512); 
	}
      if (!i_block) /* Clean Data Block Pointed by Indirect Pointer Block */
	{
	  b_head = find_bcache_entry (disk_inode->indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  for (i = 0; i < INDIRECT_BLOCK_ENTRIES && *(page_pos + i) != 0; i++)
	    free_map_release (*(page_pos + i), 1);
	  memset (b_head->b_start_page, 0, 512); 
	}
      if (!d_block) /* Clean Data Block Pointed by Inode Block */
	{
	  for (i = 0; i < DIRECT_BLOCK_ENTRIES && disk_inode->direct_map_table[i] != 0; i++)
	    {
	      free_map_release (disk_inode->direct_map_table[i], 1);
	      disk_inode->direct_map_table[i] = 0;
	    }
	}
      if (!di_pblock) /* Clean Dindirect Pointer Block */
        {
	  b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  for (f_count = 0; f_count <= p_count; f_count++)
	    free_map_release (*(page_pos + f_count), 1);
	  memset(b_head->b_start_page, 0, 512);
	  free_map_release (disk_inode->double_indirect_block_sec, 1);
	}
      if (!i_pblock) /* Clean Indirect Pointer Block */
        {
          free_map_release (disk_inode->indirect_block_sec, 1);
	}
      if (i_pblock && di_pblock && d_block && i_block && di_block)
		/* Write inode to the disk */
	{
	  if (isdir)
	    disk_inode->isdir = 1;
	  block_write (fs_device, sector, disk_inode);
	}
      free (disk_inode);
    }
  return i_pblock && di_pblock && d_block && i_block && di_block;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;


  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          //printf ("(%s) tid : %d, sector : %d, length : %d, isdir : %d\n",
	//	  __func__, thread_current ()->tid, inode->sector
	//	  ,inode->data.length, inode->data.isdir);
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
 //         printf ("(%s) tid : %d, sector : %d, length : %d, isdir : %d\n",
//		  __func__, thread_current ()->tid, inode->sector
//		  ,inode->data.length, inode->data.isdir);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
//          printf ("(%s) tid : %d, sector : %d, length : %d, isdir : %d\n",
//		  __func__, thread_current ()->tid, inode->sector
//		  ,inode->data.length, inode->data.isdir);
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

bool
inode_isdir (const struct inode *inode)
{
  return inode->data.isdir;
}

void
inode_setdir (struct inode *inode)
{
  inode->data.isdir = 1;
  block_write (fs_device, inode->sector, &inode->data);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  uint32_t i, j;
  struct buffer_head *b_head, *ib_head;
  struct inode_disk *disk_inode = NULL;
  uint32_t *page_pos = NULL, *ipage_pos = NULL;
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  
          //printf ("(%s) tid : %d, sector : %d, length : %d, isdir : %d\n",
	//	  __func__, thread_current ()->tid, inode->sector
	//	  ,inode->data.length, inode->data.isdir);
  
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      block_write (fs_device, inode->sector, &inode->data);
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
	  /* Free Disk Inode */
          free_map_release (inode->sector, 1);
	  /* Free Data Blocks */
	  disk_inode = &inode->data;
	  if (disk_inode->double_indirect_block_sec)
	  /* Free Dindirect Block Set and its Pointer blocks*/
	    {
	      b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	      page_pos = (uint32_t *) b_head->b_start_page;                      
	      for (i = 0; *(page_pos + i) != 0; j++)                          
  	        {                                                                
    		  ib_head = find_bcache_entry (*(page_pos + i));                 
    		  ipage_pos = (uint32_t *) ib_head->b_start_page;                 
    		  for (j = 0; *(ipage_pos + j) != 0; j++)              
      		    free_map_release (*(ipage_pos + j), 1); 
		  memset (ipage_pos, 0, 512);
  		} 
	      memset (page_pos, 0, 512);
      	      free_map_release (disk_inode->double_indirect_block_sec, 1); 
	    }
	  if (disk_inode->indirect_block_sec)
	  /* Free Indirect Block Set and its Pointer blocks*/
	    {
	      b_head = find_bcache_entry (disk_inode->indirect_block_sec);
	      page_pos = (uint32_t *) b_head->b_start_page;
	      for (i = 0; i < INDIRECT_BLOCK_ENTRIES && *(page_pos + i) != 0; i++)
      		free_map_release (*(page_pos + i), 1);
	      memset (page_pos, 0, 512);
      	      free_map_release (disk_inode->indirect_block_sec, 1); 
	    }
	  if (disk_inode->direct_map_table[0])
	  /* Free Direct Block Set */
	      for (i = 0; i < DIRECT_BLOCK_ENTRIES 
		&& disk_inode->direct_map_table[i] != 0; i++)
      		free_map_release (disk_inode->direct_map_table[i], 1);
	  memset(disk_inode, 0, 512);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  // uint8_t *bounce = NULL;
  struct buffer_head *b_head = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      // printf ("(%s) sector_idx : %d\n",__func__, sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;	
      /* Find Buffer Cache Entry on the list */
      b_head = find_bcache_entry (sector_idx);
      
      ASSERT (b_head != NULL);
      
      memcpy (buffer + bytes_read, b_head->b_start_page + sector_ofs, chunk_size);
      
            /*
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer. 
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy
          //   into caller's buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      */

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  // free (bounce);
	
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  // uint8_t *bounce = NULL;
  struct buffer_head *b_head = NULL, *ib_head = NULL;
  struct inode_disk *disk_inode = NULL;
  static char zeros [BLOCK_SECTOR_SIZE];

  if (inode->deny_write_cnt)
    return 0;

  int old_length = inode->data.length;
  int write_end = offset + size - 1;

  /* Support Extending File */
  if (write_end > old_length - 1)
    {
      block_sector_t old_sectors = bytes_to_sectors (old_length), 
	     new_sectors = bytes_to_sectors (write_end),
	     old_sector_pos = 0, new_sector_pos = 0,
	     i_old_sector_pos = 0, i_new_sector_pos = 0;
      uint32_t *page_pos, *ipage_pos, 
	       res_dicount, p_dicount, d_dicount, d_icount, d_dcount, sentinel;
      bool res_diblock = true, di_pblock = true, di_dblock = true, i_pblock = true,
	   i_dblock = true, d_dblock = true;
  
      disk_inode = &inode->data;
      if (new_sectors > INDIRECT_BLOCK_ENTRIES)
	{
	  //printf ("DI : old_sectors : %d, new_sectors : %d\n", old_sectors, new_sectors);
          if (old_sectors <= INDIRECT_BLOCK_ENTRIES)
	    {
	      //printf ("extend to double indirect pointer block capacity!\n");
	      ASSERT (disk_inode->double_indirect_block_sec == 0)
	      if (!free_map_allocate (1, &disk_inode->double_indirect_block_sec))
	        return 0;
	      else
		block_write (fs_device, disk_inode->double_indirect_block_sec, zeros);
	    }
	  b_head = find_bcache_entry (disk_inode->double_indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;

	  /* Allocate Indirect Pointer Block Set */
	  old_sector_pos = (old_sectors - INDIRECT_BLOCK_ENTRIES) == 0 ? 0:
		  (old_sectors - INDIRECT_BLOCK_ENTRIES) / 128;
	  new_sector_pos = (new_sectors - INDIRECT_BLOCK_ENTRIES) / 128;
	  
	  for (; old_sector_pos <= new_sector_pos; old_sector_pos++)
	    {
	      if (!*(page_pos + old_sector_pos))
		{ 
		  if(!free_map_allocate (1, page_pos + old_sector_pos))
	            {
		      di_pblock = res_diblock = false;
		      goto done;
		    }
	          else 
		    { 
	              block_write (fs_device, *(page_pos + old_sector_pos), zeros);
	  //      	printf ("[DIPOINTER] upper_block : %d "
	//				"os_pos : %d, ns_pos : %d, alloc_sector : %d\n"
			    //,disk_inode->double_indirect_block_sec 
			    //,old_sector_pos ,new_sector_pos 
			    //,*(page_pos + old_sector_pos));
		    }
		}
	    }
          b_head->b_state |= (1UL << BH_Dirty); 
	  
	  /* Fill Up Indirect Pointer Block Set */
	  old_sector_pos = (old_sectors - INDIRECT_BLOCK_ENTRIES) == 0 ? 0:
		  (old_sectors - INDIRECT_BLOCK_ENTRIES) / 128;
	  new_sector_pos = (new_sectors - INDIRECT_BLOCK_ENTRIES) / 128;
	  //printf ("[DIPOINTER] os_pos : %d, ns_pos : %d\n"
	//		      ,old_sector_pos ,new_sector_pos);
	  i_old_sector_pos = old_sectors - old_sector_pos * 128 - INDIRECT_BLOCK_ENTRIES;
	  i_new_sector_pos = new_sectors - new_sector_pos * 128 - INDIRECT_BLOCK_ENTRIES;
	  sentinel = i_new_sector_pos > i_old_sector_pos ? i_new_sector_pos - i_old_sector_pos:
		 	128; 
	  for (;old_sector_pos <= new_sector_pos; old_sector_pos++)
	    {
	      ib_head = find_bcache_entry (*(page_pos + old_sector_pos));
	      ipage_pos = (uint32_t *) ib_head->b_start_page;
	      i_old_sector_pos = old_sectors - old_sector_pos * 128 - INDIRECT_BLOCK_ENTRIES;
	      i_new_sector_pos = new_sectors - new_sector_pos * 128 - INDIRECT_BLOCK_ENTRIES;
	      sentinel = i_new_sector_pos > i_old_sector_pos ? i_new_sector_pos: 128; 
	      for (; i_old_sector_pos < sentinel; i_old_sector_pos++)
		{
		  if (!*(ipage_pos + i_old_sector_pos))
		  {
	      	    if (!free_map_allocate (1, ipage_pos + i_old_sector_pos))
	              {
			ASSERT (false);
		        di_dblock = di_pblock = res_diblock = false;
		        goto done;
		      }
	      	    else 
		      {
	                block_write (fs_device, *(ipage_pos + i_old_sector_pos), zeros);
	                //printf ("[DIDATA] upper_block : %d ios_pos : %d, ins_pos : %d"
			//		" alloc_sector : %d\n"
			//	,*(page_pos + old_sector_pos)
		          //      ,i_old_sector_pos, i_new_sector_pos 
			    //    ,*(ipage_pos + i_old_sector_pos));
		      }
		  }
		}
              ib_head->b_state |= (1UL << BH_Dirty); 
	    }
	}

      if (new_sectors > DIRECT_BLOCK_ENTRIES && old_sectors < INDIRECT_BLOCK_ENTRIES)
	{
	  //printf ("SI : old_sectors : %d, new_sectors : %d\n", old_sectors, new_sectors);
	  if (old_sectors <= DIRECT_BLOCK_ENTRIES)
	    {
	      ASSERT (disk_inode->indirect_block_sec == 0)
	      if (!free_map_allocate (1, &disk_inode->indirect_block_sec))
		{
		  i_pblock = di_dblock = di_pblock = res_diblock = false;
		  goto done;
		}
	      else
		block_write (fs_device, disk_inode->indirect_block_sec, zeros);
	    }
	  b_head = find_bcache_entry (disk_inode->indirect_block_sec);
	  page_pos = (uint32_t *) b_head->b_start_page;
	  sentinel = new_sectors - old_sectors < 128 ? 
		  (new_sectors - DIRECT_BLOCK_ENTRIES) % 129: 128;
	  d_icount = old_sectors < DIRECT_BLOCK_ENTRIES ? 0
		  : (old_sectors - DIRECT_BLOCK_ENTRIES) % 128;
	  for (; d_icount < sentinel; d_icount++)
	    {
	      if (!free_map_allocate (1, page_pos + d_icount))
		{
		  i_dblock = i_pblock = di_dblock = di_pblock = res_diblock = false;
		  goto done;
		}
	      else {
		block_write (fs_device, *(page_pos + d_icount), zeros);
	     //   printf ("d_icount : %d, sentinel : %d, alloc_sector : %d\n", d_icount, 
	//		      sentinel
	//		      ,*(page_pos + d_icount));
	      }
	    }
          b_head->b_state |= (1UL << BH_Dirty); 
	}
      if (old_sectors < DIRECT_BLOCK_ENTRIES)
	{
	  //printf ("DD : old_sectors : %d, new_sectors : %d\n", old_sectors, new_sectors);
	  sentinel = new_sectors > DIRECT_BLOCK_ENTRIES ? 
		  DIRECT_BLOCK_ENTRIES : new_sectors;
          for (d_dcount = old_sectors; d_dcount < sentinel; d_dcount++)
	    {
	      if (!free_map_allocate (1, &disk_inode->direct_map_table[d_dcount]))
		{
		  d_dblock = i_dblock = i_pblock = di_dblock = di_pblock = res_diblock = false;
		  goto done;
		}
	      else {
		block_write (fs_device, disk_inode->direct_map_table[d_dcount], zeros);
	    //    printf ("d_dcount : %d, sentinel : %d, alloc_sector : %d\n", d_dcount, 
	//		      sentinel
	//		      ,disk_inode->direct_map_table[d_dcount]);
	      }
	    }
	}
        done: /* Reclaim Allocated Blocks at the failure case */
        if (!res_diblock)
	  {
	    printf ("Stage 1\n");
	    b_head = find_bcache_entry (disk_inode->double_indirect_block_sec); 
	    page_pos = (uint32_t *) b_head->b_start_page;                       
    	    sentinel = new_sectors - old_sectors > 127 ? 128                 
        	    : (new_sectors - INDIRECT_BLOCK_ENTRIES % 128);          
    	    ib_head = find_bcache_entry (*(page_pos                          
              		+ (old_sectors - INDIRECT_BLOCK_ENTRIES)/128));        
    	    ipage_pos = (uint32_t *) ib_head->b_start_page;                  
    	    for (res_dicount = (old_sectors - INDIRECT_BLOCK_ENTRIES) % 128; 
      	    	res_dicount < sentinel; res_dicount++)                         
              free_map_release(*(ipage_pos + res_dicount), 1);         
	  }
        if (!di_dblock)
	  {
	    printf ("Stage 2\n");
	    b_head = find_bcache_entry (disk_inode->double_indirect_block_sec); 
	    page_pos = (uint32_t *) b_head->b_start_page;                       
	    for (p_dicount = DIV_ROUND_UP((old_sectors - INDIRECT_BLOCK_ENTRIES), 128);          
   	      p_dicount < DIV_ROUND_UP((new_sectors - INDIRECT_BLOCK_ENTRIES), 128); 
	      p_dicount++)
  	      {                                                                                  
    		ib_head = find_bcache_entry (*(page_pos + p_dicount));                           
    		ipage_pos = (uint32_t *) ib_head->b_start_page;                                  
    		sentinel = new_sectors - (old_sectors + p_dicount * 128) < 128 ?                     
        	    (new_sectors - INDIRECT_BLOCK_ENTRIES) % 128: 128;                       
    		for (d_dicount = 0; d_dicount < sentinel; d_dicount++)                           
        	  free_map_release (*(ipage_pos + d_dicount), 1); 
             }                                                                                  
	  }
        if (!di_pblock)
	  {
	    printf ("Stage 3\n");
	    b_head = find_bcache_entry (disk_inode->double_indirect_block_sec); 
	    page_pos = (uint32_t *) b_head->b_start_page;                       
	    for (p_dicount = DIV_ROUND_UP((old_sectors - INDIRECT_BLOCK_ENTRIES), 128);          
  	      p_dicount < DIV_ROUND_UP((new_sectors - INDIRECT_BLOCK_ENTRIES), 128); 
	      p_dicount++)
    	      free_map_release (*(page_pos + p_dicount), 1); 
	  }
        if (!i_dblock)
	  {
	    printf ("Stage 4\n");
	    b_head = find_bcache_entry (disk_inode->indirect_block_sec);              
	    page_pos = (uint32_t *) b_head->b_start_page;                             
            sentinel = new_sectors - old_sectors < 128 ?                              
        	(new_sectors - DIRECT_BLOCK_ENTRIES) % 128: 128;                  
	    for (d_icount = (old_sectors - DIRECT_BLOCK_ENTRIES) % 128;               
      	      d_icount < sentinel; d_icount++)                                    
	      free_map_release (*(page_pos + d_icount), 1);
	  }
        if (!i_pblock)
	  free_map_release (disk_inode->indirect_block_sec, 1);
        if (!d_dblock)
	  {
	    printf ("Stage 5\n");
            for (d_dcount = old_sectors; d_dcount < new_sectors ; d_dcount++)
	      free_map_release (disk_inode->direct_map_table[d_dcount], 1);
	  }
	if (!(res_diblock && di_pblock && di_dblock && i_pblock && i_dblock && d_dblock))
	  return 0;
	else
	  {
	    disk_inode->length += (write_end - old_length + 1);
	    block_write (fs_device, inode->sector, disk_inode);
	  }
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      //if (sector_idx < 2 || sector_idx > 5)printf ("(%s) sector : %d\n", __func__, sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Find Buffer Cache Entry on the list */
      b_head = find_bcache_entry (sector_idx);
      
      ASSERT (b_head != NULL);
      
      memcpy (b_head->b_start_page + sector_ofs, buffer + bytes_written, chunk_size);
      b_head->b_state |= (1UL << BH_Dirty); 

      /*
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Write full sector directly to disk. 
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          // We need a bounce buffer.
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          //   we're writing, then we need to read in the sector
          //   first.  Otherwise we start with a sector of all zeros. 
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }
      */

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

struct buffer_head *
sector_to_bhead (block_sector_t sector_idx, bool free)
{
  struct list_elem *e;	

  for (e = list_begin (&bh_list); e!= list_end (&bh_list);
	e = list_next (e))
  {
    struct buffer_head *b_head = list_entry (e, struct buffer_head, elem);
    if (!free && ((b_head->b_state & (1UL << BH_Used)) == (1UL << BH_Used))
		    && (b_head->b_blocknr == sector_idx))
      {
	list_remove (&b_head->elem);
        return b_head;
      }
    if (free && (b_head->b_state & (1UL << BH_Used)) != (1UL << BH_Used)) 
      {
	memset(b_head->b_start_page, 0, 512);
	b_head->b_state = 0;
	b_head->b_blocknr = 0;
	list_remove (&b_head->elem);
        return b_head;
      }
  }
  return NULL;
}

struct buffer_head *
evict_bcache_entry (void)
{
  struct buffer_head *b_head = NULL;

  /* Evict Least Recently Used Buffer Cache Entry */
  b_head = list_entry (list_pop_front (&bh_list), struct buffer_head, elem);
  ASSERT (b_head != NULL)
  ASSERT ((b_head->b_state & (1UL << BH_Used)) == (1UL << BH_Used))
  // printf ("(%s) state : %x, block_nr : %d\n", __func__, b_head->b_state, b_head->b_blocknr);
  if ((b_head->b_state & (1UL << BH_Dirty)) == (1UL << BH_Dirty)) /* Need to flush entry */
    block_write (fs_device, b_head->b_blocknr, b_head->b_start_page);
  b_head->b_state = 0;
  b_head->b_blocknr = 0;
  active_b_count--;
  // printf("(%s) b_head : %p count :%d\n", __func__, b_head, active_b_count);
  return b_head;
}

struct buffer_head *
find_bcache_entry(block_sector_t sector_idx)
{
  struct buffer_head *b_head = NULL;

  b_head = sector_to_bhead (sector_idx, false);
  if (!b_head) /* Block is not on Buffer Cache */
    {
      if (active_b_count == BH_ENTRY) /* Need to evict LRU Buffer Cache Entry */	    
        b_head = evict_bcache_entry();
      else /* Find Free Buffer Cache Entry */
        b_head = sector_to_bhead (0, true);
      /* Initialize Free Buffer Cache Entry */
      ASSERT (b_head != NULL)
      b_head->b_state |= (1UL << BH_Used); 
      b_head->b_blocknr = sector_idx;
      block_read (fs_device, sector_idx, b_head->b_start_page);
      active_b_count++;
    }
    /* Add Buffer Cache Entry to the LRU List */
    list_push_back (&bh_list, &b_head->elem);
  return b_head;
}

void /* Periodic Flush of dirty block */ 
pdflush (void)
{
  struct list_elem *e;	

  for (e = list_begin (&bh_list); e!= list_end (&bh_list);
	e = list_next (e))
  {
    struct buffer_head *b_head = list_entry (e, struct buffer_head, elem);
    if (((b_head->b_state & (1UL << BH_Used)) == (1UL << BH_Used))
    	&& ((b_head->b_state & (1UL << BH_Dirty)) == (1UL << BH_Dirty)))
      block_write (fs_device, b_head->b_blocknr, b_head->b_start_page);
  }
}

bool
inode_is_removed (struct inode *arg_node)
{
  return arg_node->removed;
}
