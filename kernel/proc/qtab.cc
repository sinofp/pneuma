#include "proc/qtab.h"

qent qtab[NQENT];

void queue_init() {
    // TODO remove?
    for (size_t i = 0; i < PROCS_MAX; ++i)
        qtab[i] = { 0, EMPTY, EMPTY };

    // Initialize each queue's head/tail pair
    for (qid_t head = PROCS_MAX; head < NQENT; head += 2) {
        qid_t tail = head + 1;

        // The MINKEY at tail is used to ensure the descending priority queue can find a place to insert.
        qtab[head] = { MAXKEY, tail, EMPTY };
        qtab[tail] = { MINKEY, EMPTY, head };
    }
}
