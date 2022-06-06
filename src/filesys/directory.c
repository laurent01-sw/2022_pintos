#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/free-map.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
//struct dir_entry 
//  {
//    block_sector_t inode_sector;        /* Sector number of header. */
//    char name[NAME_MAX + 1];            /* Null terminated file name. */
//    bool in_use;                        /* In use or free? */
//  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, 0, true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    {
      // printf ("(%s) sector : %d, name : %s, in_use %d\n"
	//	      ,__func__ ,e.inode_sector, e.name, e.in_use);
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
    }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  // ASSERT (dir != NULL);
  if (dir == NULL) return false;

  ASSERT (name != NULL);

  inode_lock (dir_get_inode ((struct dir *) dir));

  // printf ("(%s) dir : %p, name : %s\n" ,__func__, dir, name);
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  inode_unlock (dir_get_inode ((struct dir *) dir));

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  inode_lock (dir_get_inode (dir));

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    goto done;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set parent */
  if (!inode_set_parent (inode_get_inumber (dir_get_inode (dir)), inode_sector))
  {
    goto done;
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  // printf ("(%s) sector : %d, name : %s, in_use : %d\n"
	//	  ,__func__, e.inode_sector, e.name, e.in_use);

 done:
  inode_unlock (dir_get_inode (dir));
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  inode_lock (dir_get_inode (dir));

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  // printf ("dir_remove, Found: %s\n", name);

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Prevent directory that have entries from removing */
  if (inode_isdir (inode))
    {
      off_t dir_ofs; 
      struct dir_entry e_ite;
      
      for (dir_ofs = 0; 
        inode_read_at (inode, &e_ite, sizeof e_ite, dir_ofs) == sizeof e;
	      dir_ofs += sizeof e_ite)
        {
          if (e_ite.in_use)
            goto done;
        }
    }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);

  inode_unlock (dir_get_inode (dir));
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  inode_lock (dir_get_inode (dir));

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          
          inode_unlock (dir_get_inode (dir));
          return true;
        } 
    }

  inode_unlock (dir_get_inode (dir));
  return false;
}

struct dir *
find_end_dir (const char *name, char **filename, bool create) 
{
  /* Parse the path, need to implement relative path function */     
  struct dir *dir = (*name == '/' || !(thread_current ()->current_dir)) 
	  ? dir_open_root () : dir_reopen (thread_current ()->current_dir), *next_dir;

  struct inode *inode = NULL;
  block_sector_t inode_sector = 0;

  char *s = name;
  char *token, *save_ptr;       

  uint32_t depth = 0, i = 0;

  const char *p = name;
  if (*p != '/') depth++;  
  for (p = name; *p != '\0'; p++)                                                           
    if (*p == '/' && *(p + 1) != '\0' && *(p + 1) != '/') 
      depth++;                               

  char *parsed_dir [depth];        

  for (token = strtok_r (s, "/", &save_ptr); token != NULL;          
      token = strtok_r (NULL, "/", &save_ptr))                     
    {                                                                
      parsed_dir[i++] = token;                                       
      // printf ("'%d : %s' ", i - 1, token);                    
      if (dir_lookup (dir, token, &inode) && create && i == depth && inode)
	      return NULL;	      
      
      if (!inode)
        { 
          // printf ("(%s) no entry found for (%s)\n", __func__, token);
          if (create && i == depth)
            {

              free_map_allocate (1, &inode_sector);
              dir_create (inode_sector, 16);
              if (dir_add (dir, token, inode_sector))
                {
                  inode = inode_open (inode_sector);
                  return dir_open (inode);
                }
              else
                {
                  free_map_release (inode_sector, 1);
                  return NULL;
                }
            }
          else
            {
              *filename = token;
              return dir;
            }
        }

      if (inode_isdir (inode))                                         
        {                                                            
          if (i == depth)
            {
              *filename = token;
              return dir;
            }

          next_dir = dir_open (inode);                                    
          dir_close (dir);
          dir = next_dir;
	  
        }                                                            
      else /* Check whether regular File is in the middle */         
        {                                                            
          if (i != depth)                                            
            {                                                        
              printf ("(%s) Regular File in the middle of the path!\n",__func__);
              printf ("name : %s, current depth : %d, current elem : %s\n",name, i, token);
              return NULL;
            }                                                        
          else
            *filename = token;
        }                                                            
    }                                                                

  return dir;
}

struct inode*
dir_to_inode (const struct dir *dir)
{
  return dir->inode;
}



struct inode* 
dir_get_parent_inode (struct dir* dir)
{
  if (dir == NULL) return NULL;
  
  block_sector_t sector = inode_get_parent (dir_get_inode (dir));
  return inode_open (sector);
}
