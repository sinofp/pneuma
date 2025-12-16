#pragma once

#include "proc.h"
#include "qtab.h"

inline void print_readyq() {
    auto readyq = ready_queue();
    printf("readyq content (first pid is %d):\n", readyq.first_id());
    for (const auto &qe : readyq)
        printf("  prio: %d, prev: %d, next: %d\n", qe.key, qe.prev, qe.next);
}

inline void print_procs() {
    printf("procs content:\n");
    for (int i = 0; i < PROCS_MAX; ++i)
        printf("  i: %d, pid: 0x%x, pid_addr: %p, state: 0x%x, sp: %p, stack range [%p, %p]\n", i, procs[i].pid,
               &procs[i].pid, static_cast<int>(procs[i].state), reinterpret_cast<void *>(procs[i].sp), procs[i].stack,
               &procs[i].stack[sizeof(procs[i].stack)]);
}
