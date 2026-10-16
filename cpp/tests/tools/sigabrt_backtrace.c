#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void handler(int sig) {
    void* bt[48];
    int n = backtrace(bt, 48);
    dprintf(2, "\n=== BACKTRACE (signal %d) ===\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}
__attribute__((constructor)) static void install(void) {
    signal(SIGABRT, handler);
    signal(SIGSEGV, handler);
}
