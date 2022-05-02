#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/exception.h"

// Project 3. VM
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/block.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/mmap.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}



// Declare
void
handle_mm_fault (
        struct intr_frame *f,
        void *fault_addr,
        bool,
        bool,
        bool
    );

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
  
  // Project 3: VM
  handle_mm_fault (f, fault_addr, not_present, write, user);

  return;

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
          printf("eip: %p\n", f->eip);

  __exit(-1);
  kill (f);
}


extern struct lock elf_load_lock;
extern struct lock filesys_lock;

/* Project 3: VM, Added.
 * New page fault handler added.
 */

/* Complement functions:
 *  - is_allowed_addr
 *  - 
 */


#define KILL_APP(F) \
   do { __exit (-1); kill (F); } while (0)


void
handle_mm_fault (
        struct intr_frame *f,
        void *fault_addr,
        bool not_present, 
        bool write, 
        bool user
    )
{
   // printf ("handle_mm_fault: %p\n", fault_addr);
   struct vm_entry *vme;
   struct thread *cur = thread_current ();
   
   if (!not_present)
      KILL_APP (f);

   // 1. Address range check
   if (!is_allowed_addr (f, fault_addr))
      KILL_APP (f);

   // 2. Is swapped?
   vme = find_vme (&(cur->vm), pg_round_down (fault_addr));
   if (vme != NULL && vme->si.loc == DISK)
   {
      vme->pf->pinned = true;

      if (!swap_in(&(cur->vm), vme))
         ASSERT (false);

      vme->pf->pinned = false;
      return;
   }


#define __VALID_STACK__(ESP, FA) \
   (ESP <= FA || FA == ESP - 4 || FA == ESP - 32) && \
   ((PHYS_BASE - pg_round_down (FA)) <= (1 << 23) \
      && (uint32_t*)FA >= (ESP - 32))

   
   if (vme == NULL)
   {
      if (__VALID_STACK__(f->esp, fault_addr)) // Check valid stack
         handle_stack_fault (f, fault_addr);

      else
         KILL_APP (f);

      return;
   }

   // 3. Cases handling
   if (vme->writable == false && write)
      KILL_APP (f);

   if (vme->page_type == MMAP)
      handle_mmap_fault (fault_addr);

   else if (vme->page_type == ELF)
      handle_load_elf (fault_addr);

   else
      KILL_APP (f);
   
   return ;
}


// Checker Functions
// Checks address range
bool 
is_allowed_addr (struct intr_frame *f, void *fault_addr)
{
   return 
      fault_addr < ((void *) 0x08048000)  ? false :
      fault_addr >= PHYS_BASE             ? false : 
      true;
}


bool 
is_stack_access (struct intr_frame *f, void *fault_addr)
{
   if (f->esp < fault_addr) return true;
   
   bool res = ((PHYS_BASE - pg_round_down (fault_addr)) <= (1 << 23) 
         && (uint32_t*)fault_addr >= (f->esp - 32));
   // printf ("addr: %p, %d\n", fault_addr, res);
   return res;
}


// Demand paging for ELF load
void 
handle_load_elf (void *fault_addr)
{
   struct thread *t = thread_current ();
   struct vm_entry *vme;
   uint8_t *kpage;

   fault_addr = pg_round_down (fault_addr);

   vme = find_vme (&(t->vm), fault_addr);
   
   kpage =  alloc_pframe (PAL_USER);
   
   ASSERT (vme != NULL);
   ASSERT (kpage != NULL);

   // Lookup the disk
   file_seek (vme->ti.exe_file, vme->ti.ofs);

   if (file_read (vme->ti.exe_file, kpage, vme->ti.rbytes) != (int) vme->ti.rbytes)
   {
      palloc_free_page (kpage);
      ASSERT (false);
   }

   memset (kpage + vme->ti.rbytes, 0, vme->ti.zbytes);

   vme->paddr = kpage;
   vme->si.loc = MEMORY;

   if (!install_page (vme->vaddr, kpage, vme->writable)) 
   {
      palloc_free_page (kpage);
      ASSERT (false);
   }
}


void 
handle_stack_fault (struct intr_frame *f, void *fault_addr)
{
   struct thread *t = thread_current ();
   struct vm_entry *vme;
   uint8_t *kpage;

   vme = find_vme (&(t->vm), pg_round_down (fault_addr) + PGSIZE);

   if (vme == NULL) // Make it continuous, for large stack
      handle_stack_fault (f, fault_addr + PGSIZE);
   
   vme = malloc (sizeof (struct vm_entry));
   // kpage = palloc_get_page (PAL_USER | PAL_ZERO);
   kpage = alloc_pframe (PAL_USER | PAL_ZERO);

   struct text_info tinfo = {
      .owner    = t,
      .exe_file = 0,
      .ofs      = 0,
      .rbytes   = 0,
      .zbytes   = 0
   };  // Meaningless.

   struct swap_info sinfo = {
      .loc      = MEMORY,
      .blk_idx  = vme->si.blk_idx
   };

   struct mmap_info minfo = {
      .loc        = VALHALLA,
      .fobj       = NULL,
      .fd         = 0,
      .ofs        = 0,
      .rbytes     = 0,
      .zbytes     = 0,
      .self       = vme
   };

   ASSERT (vme != NULL);

   init_vm_entry (
            vme,
            pg_round_down (fault_addr), // Set upage (vaddr). Recall we are dealing with stack!
            true, // Writable permission
            &tinfo,
            &sinfo,
            &minfo,
            ANONYMOUS
      );

   if (!install_page (pg_round_down (fault_addr), kpage, true))
   {
      free (kpage);
      ASSERT (false); // Raise panic.
   }

   vme->paddr = kpage;

   if (!insert_vme ( &(t->vm), vme))
   {
      free (vme);
      ASSERT (false); // Raise panic.
   }   
}


void
handle_mmap_fault (void *fault_addr)
{
   struct thread *t = thread_current ();
   struct vm_entry *vme;
   uint8_t *kpage;

   // printf ("handle_mmap_fault\n");

   fault_addr = pg_round_down (fault_addr);

   vme = find_vme (&(t->vm), fault_addr);
   kpage = alloc_pframe (PAL_USER | PAL_ZERO);
   
   ASSERT (vme != NULL);
   ASSERT (kpage != NULL);

   // Lookup the disk
   lock_acquire (&filesys_lock);

   file_seek (vme->mi.fobj, vme->mi.ofs);

   if (file_read (vme->mi.fobj, kpage, vme->mi.rbytes) != (int) vme->mi.rbytes)
   {
      palloc_free_page (kpage);
      ASSERT (false);
   }

   lock_release (&filesys_lock);

   memset (kpage + vme->mi.rbytes, 0, vme->mi.zbytes);

   vme->paddr = kpage;
   vme->mi.loc = MEMORY;

   if (!install_page (vme->vaddr, kpage, vme->writable)) 
   {
      palloc_free_page (kpage);
      ASSERT (false);
   }
}


void 
handle_null (void *fault_addr, bool writable)
{
   struct thread *t = thread_current ();
   struct vm_entry *vme;
   uint8_t *kpage;
   
   vme = malloc (sizeof (struct vm_entry));
   
   kpage = alloc_pframe (PAL_USER | PAL_ZERO);

   struct text_info tinfo = {
      .owner    = t,
      .exe_file = 0,
      .ofs      = 0,
      .rbytes   = 0,
      .zbytes   = 0
   };  // Meaningless.

   struct swap_info sinfo = {
      .loc      = MEMORY,
      .blk_idx  = vme->si.blk_idx
   };

   struct mmap_info minfo = {
      .loc        = MEMORY,
      .fobj       = NULL,
      .fd         = 0,
      .ofs        = 0,
      .rbytes     = 0,
      .zbytes     = 0,
      .self       = vme
   };

   ASSERT (vme != NULL);

   init_vm_entry (
            vme,
            pg_round_down (fault_addr),   // Set upage (vaddr). Recall we are dealing with stack!
            writable,                     // Writable permission
            &tinfo,
            &sinfo,
            &minfo,
            ANONYMOUS
      );

   if (!install_page (pg_round_down (fault_addr), kpage, true))
   {
      free (kpage);
      ASSERT (false); // Raise panic.
   }

   vme->paddr = kpage;

   if (!insert_vme ( &(t->vm), vme))
   {
      free (vme);
      ASSERT (false); // Raise panic.
   }
}



void
borrow_stack (struct intr_frame *f, void *addr, uint32_t size)
{
   void *req_addr = pg_round_down (addr);

   while (req_addr < addr + size)
   {
      if (__VALID_STACK__(f->esp, addr)) // Check valid stack
         handle_stack_fault (f, addr);

      req_addr += PGSIZE;
   }
}


void
return_stack (void *addr, uint32_t size)
{
   void *req_addr = pg_round_down (addr);
   struct vm_entry *vme;

    while (req_addr < req_addr + size)
    {
      delete_vme (
         &(thread_current ()->vm), 
         find_vme (&(thread_current ()->vm), req_addr)
      );

      req_addr += PGSIZE;      
    }
}


void
force_reserve_pages (void *addr, uint32_t size)
{
   void *req_addr = pg_round_down (addr);
   while (req_addr < addr + size)
   {
      swap_out ();
      req_addr += PGSIZE;
   }
}