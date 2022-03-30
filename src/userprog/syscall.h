#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>

void syscall_init (void);

int32_t __exit(int status);

#endif /* userprog/syscall.h */
