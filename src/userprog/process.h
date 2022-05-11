#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

// Added.

bool install_page (void *upage, void *kpage, bool writable);
bool install_hpage (void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
