#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

void exception_init (void);
void exception_print_stats (void);

// Project 3.
bool is_allowed_addr (struct intr_frame *, void *);
bool is_stack_access (struct intr_frame*, void *);

void handle_load_elf (void *);
void handle_stack_fault (struct intr_frame*, void *);
void handle_mmap_fault (void *);

void handle_null (void *, bool);


void borrow_stack (struct intr_frame *f, void *addr, uint32_t size);
void return_stack (void *addr, uint32_t size);
void force_reserve_pages (void *addr, uint32_t size);

#endif /* userprog/exception.h */
