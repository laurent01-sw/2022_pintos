#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

extern struct list bh_list;
extern struct lock bh_list_lock;

static void do_format (void);
static void bcache_init (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  /* Initialize Buffer Cache Infrastructure */
  bcache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  pdflush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  char s[strlen(name) + 1];
  char *filename = NULL;

  /* These two must be prohibited. */
  if (strcmp (name, ".") == 0) return false;
  if (strcmp (name, "..") == 0) return false;

  strlcpy (s, name, strlen(name) + 1);
  dir = find_end_dir (s, &filename, false);
  
  bool success = false;

  /* Check if directory is valid */
  if (inode_is_removed (dir_get_inode (dir)))
  {
    dir_close (dir);
    return success;
  }

  success = (dir != NULL && filename != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = NULL;
  char s[strlen(name) + 1];
  char *filename = NULL;
  struct inode *inode = NULL;

  struct dir *cur_dir = thread_current ()->current_dir;

  strlcpy (s, name, strlen(name) + 1);

  if (*name == '\0')
    return NULL;

  /* Case 1: Requests Root */
  if (!strcmp(s, "/"))
    return file_open (dir_to_inode (dir_open_root ()));

  /* Case 2: Requests Parent */
  if (
    strcmp (name, "..") == 0
    && !inode_is_removed (dir_to_inode (cur_dir))
    )
    {
      inode = dir_get_parent_inode (cur_dir); 
      inode_close (inode);
    }

  /* Case 3: Requests Self */
  else if (strcmp (name, ".") == 0)
    {
      inode = dir_to_inode (cur_dir); // here?
    }
  
  /* Case 4: Requests Arbitrary */
  else {

    dir = find_end_dir (s, &filename, false);
    
    if (dir != NULL)
    {
      dir_lookup (dir, filename, &inode);
      dir_close (dir);
    }
  }

  if (inode == NULL)
    return NULL;

  if (inode_is_removed (inode))
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = NULL;
  struct inode *inode = NULL;
  char s[strlen(name) + 1];
  char *filename = NULL;

  strlcpy (s, name, strlen(name) + 1);
  dir = find_end_dir (s, &filename, false);

  if (dir != NULL && !strcmp (s,"/"))
    return false;
  
  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

static void 
bcache_init (void)
{
  int i;
  struct buffer_head *b_head;
  char *b_page;

  list_init (&bh_list); 
  lock_init (&bh_list_lock);

  b_page = NULL;

  for (i = 0; i < BH_ENTRY; i++) 
    {
      b_head = malloc (sizeof *b_head);
      memset (b_head, 0, sizeof *b_head);

      if (i % 4 == 0) /* Alloc Page for each 4th buffer head */
	{
	  b_page = palloc_get_page (PAL_ZERO); 
	}
      b_head->b_magic = BH_MAGIC;
      b_head->b_page = (uint32_t *) b_page;
      b_head->b_start_page = b_page +  BLOCK_SECTOR_SIZE * (i % 4);
      b_head->pos = BLOCK_SECTOR_SIZE * (i % 4);  

      list_push_back (&bh_list, &b_head->elem);
    } 
}
