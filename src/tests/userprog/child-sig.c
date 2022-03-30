/* Child process run by exec-multiple, exec-one, wait-simple, and
   wait-twice tests.
   Just prints a single message and terminates. */

#include <stdio.h>
#include <string.h>
#include <syscall.h>

#include "tests/lib.h"

void sig_handler1(void);
void sig_handler2(void);

int main(void) {
    test_name = "child-sig";

    sigaction(SIGONE, sig_handler1);
    sigaction(SIGTWO, sig_handler2);
    msg("run");
    sched_yield();
    return 81;
}

void sig_handler1(void) {
}

void sig_handler2(void) {
}