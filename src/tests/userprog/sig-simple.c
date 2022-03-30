/* Executes and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void sig_handler (void);

void
test_main (void) 
{
  pid_t child_pid = exec ("child-sig");
  sendsig (child_pid, SIGONE);
  sendsig (child_pid, SIGTWO);
  sendsig (child_pid, SIGTHREE);
  wait (child_pid);
}
