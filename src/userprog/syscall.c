#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"


static void syscall_handler (struct intr_frame *);
static bool check_address(void * address);
static bool check_filename_address(void * address);

struct lock filesys_lock;
extern struct list all_list;

// static uint32_t file_descriptor_remain = 2; // 0, 1, and 2 might be occupied.
static uint32_t allocate_fd(void); // used to allocate file descriptor
int32_t __exit(int);

static bool bad_ptr(void*, struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init(&filesys_lock);
  list_init(&all_list);
}

static void
syscall_handler (struct intr_frame *f)                         
{                                                              
  unsigned int *status, *fd;                                            
  char** file;                                                 
  unsigned *initial_size, *size, *position;                    
  void** buffer;                                               
  void* usp = f->esp;                                          
  struct file* f_temp;                                         
  unsigned int i;                                                       
  struct thread *cur = thread_current ();                      
                                                               
  if(bad_ptr(usp,f)) return;                                   
                                                               
  uint32_t syscall_num = *((uint32_t*)usp);                    
  //printf("System Call Number : %d\n",syscall_num);           
  switch (syscall_num)                                         
  {                                                            
  case SYS_HALT: // (void)                                     
    shutdown_power_off();                                      
    break;                                                     
  case SYS_EXIT: // (int status)                               
    if(bad_ptr(usp + 4, f)) break;                             
                                                               
    status = usp + 4;                                          
    __exit(*status);                                             
    f->eax = *status; // NEVER REACHED                         
    break;                                                     
  case SYS_FILESIZE: // (int fd)                               
    if(bad_ptr(usp + 4, f)) break;                          
    lock_acquire(&filesys_lock);
    fd = usp + 4;                                              
    for(i = 0; i < cur->fd_pos; i++)                           
    {                                                          
      if(cur->fd[i] == *fd)                                    
      {                                                        
        f_temp = cur->fd_file[i];                              
        f->eax = file_length(f_temp);                          
      }                                                        
    }                                                          
    lock_release(&filesys_lock);
    break;                                                     
  case SYS_CREATE: // (const char* file, unsigned initial_size)
    if(bad_ptr(usp + 16, f)) break;                            
    if(bad_ptr(usp + 20, f)) break;                            
	
    file = usp + 16;                                
    initial_size = usp + 20;                        
                                                
    if(!check_filename_address(*file))                       
    {                                               
      f->eax = -1;                                  
      __exit(-1);                                     
    }                                               
    else 
    {                                        
      //printf("filename : %s, size :%d\n",*file,*initial_size);
      lock_acquire(&filesys_lock);
      f->eax = filesys_create(*file, *initial_size);
      lock_release(&filesys_lock);
    }      
    break;                                          
  case SYS_OPEN:
    if (bad_ptr(usp + 4, f)) break;

    file = usp + 4;

    if(!check_filename_address(*file))
    {
      f->eax = -1;
      __exit(-1);
    }
    else
    {
      //printf("filename : %s\n",*file);
      lock_acquire(&filesys_lock);
      f_temp = filesys_open(*file); 
      if(f_temp == NULL)
        f->eax = -1;
      else
      {
        f->eax = allocate_fd();
        cur->fd[cur->fd_pos] = f->eax;
        cur->fd_file[cur->fd_pos] = f_temp;
        cur->fd_pos += 1;
      }
      lock_release(&filesys_lock);
    }
    break;
  case SYS_CLOSE: // (int fd)
    if(bad_ptr(usp + 4, f)) break;

    lock_acquire(&filesys_lock);
    fd = usp + 4;

    for(i = 0; i < cur->fd_pos; i++)
    {
      if(cur->fd[i] == *fd)
      {
        file_close(cur->fd_file[i]);
        cur->fd_pos -= 1;
        cur->fd[i] = cur->fd[cur->fd_pos];
        cur->fd_file[i] = cur->fd_file[cur->fd_pos];
      }
    }
    lock_release(&filesys_lock);
    break;
  case SYS_WRITE: // (int fd, const void* buffer, unsigned size)
    if(bad_ptr(usp + 20, f)) break;
    if(bad_ptr(usp + 24, f)) break;
    if(bad_ptr(usp + 28, f)) break;

    fd = usp + 20;
    buffer = usp + 24;
    size = usp + 28;

    if(!check_address(*buffer))
    {
      f->eax = -1;
      __exit(-1);
    }
    else if(*fd == 1)
    {
      putbuf(*buffer, *size);
      f->eax = *size;
    }
    else if(*fd > 1)
    {
      lock_acquire(&filesys_lock);
      for(i=0;i<cur->fd_pos;i++)
      {
        if(cur->fd[i] == *fd)
        {
          if(cur->fd_file[i] == NULL)
            __exit(-1);
          else
            f->eax = file_write(cur->fd_file[i],*buffer,*size);
        }
      }
      lock_release(&filesys_lock);
    }
    break;
  case SYS_REMOVE:                
    if(bad_ptr(usp + 4, f)) break;
    file = usp + 4;               
    if(!check_filename_address(*file))     
    {                             
      f->eax = -1;                
      __exit(-1);                   
    }
    lock_acquire(&filesys_lock);
    f->eax = filesys_remove(*file);
    lock_release(&filesys_lock);
    break;                        
  case SYS_EXEC:                          
    if(bad_ptr(usp + 4, f)) break;        
    file = usp + 4;                       
    if(!check_filename_address(*file))             
    {                                     
      f->eax = -1;                        
      __exit(-1);                           
    }                                     
    f->eax = process_execute(*file);      
    tid_t ctid = thread_current ()->ctid; 
    struct thread *ct = thread_wait(ctid);
    if(ct == NULL){                       
      f->eax = -1;                        
      break;                              
    }                                     
    sema_down(&(ct->load_sema));          
    if(!ct->load_status)                  
    {                                     
      f->eax = -1;                        
    }                                     
    break;                                
  case SYS_READ:
    if(bad_ptr(usp + 20, f)) break;
    if(bad_ptr(usp + 24, f)) break;
    if(bad_ptr(usp + 28, f)) break;

    lock_acquire(&filesys_lock);
    fd = usp + 20;
    buffer = usp + 24;
    size = usp + 28;
    if(!check_address(*buffer))
    {
      f->eax = -1;
      lock_release(&filesys_lock);
      __exit(-1);
    }
    else if(*fd == 0)
    {
      for(i = 0; i < size; i++)
      {
        *(buffer++) = input_getc();
      }
    }
    else if(*fd > 1)
    {
      for(i=0;i<cur->fd_pos;i++)
      {
        if(cur->fd[i] == *fd)
        {
          if(cur->fd_file[i] == NULL)
          {
            f->eax = -1;
            lock_release(&filesys_lock);
            __exit(-1);
          }
          else
          {
            f->eax = file_read(cur->fd_file[i],*buffer,*size);
          }
        }
      }
    }
    lock_release(&filesys_lock);
    break;
  case SYS_WAIT:                                                 
    if(bad_ptr(usp + 4, f)) break;                               
    tid_t child_tid = *(tid_t *)(usp + 4);                       
    f->eax = process_wait(child_tid);                            
    break;                                                       
  case SYS_SEEK: // (int fd, unsigned position)                  
    if(bad_ptr(usp + 16, f)) break;                              
    if(bad_ptr(usp + 20, f)) break;                              
                                                               
    lock_acquire(&filesys_lock);
    fd = usp + 16;                                               
    position = usp + 20;                                         
    for(i=0;i<cur->fd_pos;i++)                                   
    {                                                            
      if(cur->fd[i] == *fd)                                      
      {                                                          
        if(cur->fd_file[i] == NULL)                              
        {                                                        
          f->eax = -1;                                           
          lock_release(&filesys_lock);
          __exit(-1);                                              
        }                                                        
        else                                                     
        {                                                        
          //printf("Sucessfully match fd and file structure\n"); 
          file_seek(cur->fd_file[i],*position);                  
          //printf("wanted : %d, real : %d\n",f->eax,*size);     
        }                                                        
      }                                                          
    }                                                            
    lock_release(&filesys_lock);
    break;
  }
}

static bool // Modified
check_address(void * uva)
{
  if(!is_user_vaddr(uva))
    return false;
  uint32_t* pd;
  struct thread *cur = thread_current ();
  pd = cur->pagedir;
  if(!pagedir_get_page(pd, uva))
    return false;
  return true;
}

static bool
check_filename_address(void * filename)
{
  char *p;
  for (p = filename;;p++)
  {
    if (!check_address(p))
      return false;
    else if (*p == '\0')
      return true;
  } 
}

static uint32_t // May be changed to allocate empty integer after 1
allocate_fd(void) // used to allocate file descriptor
{
  static uint32_t file_descriptor_remain = 2; // 0, 1, and 2 might be occupied.
  uint32_t _return_;
  _return_ = file_descriptor_remain++;
  return _return_;
}

int32_t __exit(int status)
{
    printf("%s: exit(%d)\n",thread_name(), status);
    struct thread *cur = thread_current ();
    unsigned int i;
    for(i = 0; i < cur->fd_pos; i++)
    {
      file_close(cur->fd_file[i]);
    }
    struct list_elem* e;
    cur->exit_status = status;
    /* Cleaning Up Child Process */
    for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if(t->ptid == cur->tid && !t->seen_status)
        process_wait(t->tid);
    }
    lock_acquire(&filesys_lock);
    file_allow_write(cur->itself);
    lock_release(&filesys_lock);
    thread_exit();
}

static bool bad_ptr(void* ptr, struct intr_frame *f)
{
  unsigned int i;
  for (i = 0; i < 4; i++) 
  {
    if(!check_address(ptr + i))
    {
      __exit(-1);
      f->eax = -1;
      return true;
    }
  }
  return false;
}

