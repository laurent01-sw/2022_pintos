#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* System call numbers. */
enum 
  {
    /* Projects 2 and later. */
    SYS_HALT,                   /* 0: Halt the operating system. */
    SYS_EXIT,                   /* 1: Terminate this process. */
    SYS_EXEC,                   /* 2: Start another process. */
    SYS_WAIT,                   /* 3: Wait for a child process to die. */
    SYS_CREATE,                 /* 4: Create a file. */
    SYS_REMOVE,                 /* 5: Delete a file. */
    SYS_OPEN,                   /* 6: Open a file. */
    SYS_FILESIZE,               /* 7: Obtain a file's size. */
    SYS_READ,                   /* 8: Read from a file. */
    SYS_WRITE,                  /* 9: Write to a file. */
    SYS_SEEK,                   /* 10: Change position in a file. */
    SYS_TELL,                   /* 11: Report current position in a file. */
    SYS_CLOSE,                  /* 12: Close a file. */
    SYS_SIGACTION,              /* 13: Register an signal handler */
    SYS_SENDSIG,                /* 14: Send a signal */
    SYS_YIELD,                  /* 15: Yield current thread */

    /* Project 3 and optionally project 4. */
    SYS_MMAP,                   /* 16: Map a file into memory. */
    SYS_MUNMAP,                 /* 17: Remove a memory mapping. */

    /* Project 4 only. */
    SYS_CHDIR,                  /* 18: Change the current directory. */
    SYS_MKDIR,                  /* 19: Create a directory. */
    SYS_READDIR,                /* 20: Reads a directory entry. */
    SYS_ISDIR,                  /* 21: Tests if a fd represents a directory. */
    SYS_INUMBER                 /* 22: Returns the inode number for a fd. */
  };

#endif /* lib/syscall-nr.h */
