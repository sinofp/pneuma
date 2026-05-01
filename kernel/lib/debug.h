#pragma once

#include "proc/proc.h"
#include "proc/qtab.h"

inline void print(proc::queue_view (*queue)()) {
    auto q = queue();
    auto pid = q.first_id();
    for (const auto &qe : q) {
        printf("  pid: %d, key: %d, prev: ", pid, qe.key);
        if (q.head_id() == qe.prev)
            printf("h");
        else
            printf("%d", qe.prev);
        printf(", next: ");
        if (q.tail_id() == qe.next)
            printf("t\n");
        else
            printf("%d\n", qe.next);
        pid = qe.next;
    }
}

inline void print_procs() {
    using namespace proc;
    printf("procs content:\n");
    for (int i = 0; i < MAX; ++i)
        printf("  i: %d, pid: 0x%x, pid_addr: %p, state: 0x%x, sp: %p, stack range [%p, %p]\n", i, procs[i].pid,
               &procs[i].pid, static_cast<int>(procs[i].state), reinterpret_cast<void *>(procs[i].sp), procs[i].stack,
               &procs[i].stack[sizeof(procs[i].stack)]);
}
