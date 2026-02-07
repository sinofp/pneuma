#pragma once

#include "lib/optional.h"
#include "proc/proc.h"

// TODO wrap everytihing in proc namespace?

using qkey_t = int32_t;   // Use int for minus MINKEY TODO better approach
using qid_t = proc::id_t; // Since the index is pid

// TODO consider removing it because we don't rely on this special value for empty()
constexpr qid_t EMPTY = -1;
constexpr qkey_t MAXKEY = 0x7FFFFFFF;
constexpr qkey_t MINKEY = 0x80000000;

constexpr size_t NQUEUES = 2;                       // # of different queues (ready, sleep, etc.)
constexpr size_t NQENT = proc::MAX + (NQUEUES * 2); // * 2 for head & tail
constexpr qid_t READY_QUEUE = proc::MAX;
constexpr qid_t SLEEP_QUEUE = proc::MAX + 2;

struct qent {
    qkey_t key;
    qid_t next;
    qid_t prev;
};

// I must declare it here for queue_view inline methods.
// I might make the methods non-inline because I don't want other source files able to access qtab directly.
extern qent qtab[];

namespace queue {
void init();
} // namespace queue

// 'Different' queues that uses qtab
class queue_view {
    qid_t q;

public:
    constexpr qid_t head_id() const { return q; }

    constexpr qid_t tail_id() const { return q + 1; }

    qid_t first_id() const { return qtab[head_id()].next; }

    // TODO remove?
    // qid_t last_id() const { return qtab[tail_id()].prev; }

    explicit constexpr queue_view(qid_t id)
        : q(id) {}

    // For range-based for loop
    struct iterator {
        qid_t curr;

        const qent &operator*() const { return qtab[curr]; }

        iterator &operator++() {
            curr = qtab[curr].next;
            return *this;
        }

        bool operator!=(qid_t sentinel) const { return curr != sentinel; }
    };

    iterator begin() const { return iterator{ first_id() }; }

    qid_t end() const { return tail_id(); }

    // Queue is empty if first entry points to something >= proc::MAX (i.e., tail)
    // TODO change it to first_id() == tail_id()?
    bool empty() const { return first_id() >= static_cast<qid_t>(proc::MAX); }

    // Return a reference so we can modify (for sleep queue)
    qkey_t &first_key() const { return qtab[first_id()].key; }

    // Insert at the end.
    // This method is only used for FIFO queues, so we ignore their key (priority)
    inline optional<proc::id_t> enqueue(proc::id_t pid) {
        if (proc::bad_id(pid)) {
            return nullopt;
        }

        auto tail = tail_id();
        auto prev = qtab[tail].prev;

        qtab[pid].next = tail;
        qtab[pid].prev = prev;

        qtab[prev].next = pid;
        qtab[tail].prev = pid;

        return pid;
    }

    inline optional<qid_t> dequeue() {
        if (empty()) {
            return nullopt;
        }

        auto pid = first_id();

        auto second_id = qtab[pid].next;
        qtab[head_id()].next = second_id;
        qtab[second_id].prev = head_id();

        // TODO remove?
        qtab[pid] = { 0, EMPTY, EMPTY };

        return pid;
    }

    inline optional<proc::id_t> insert_desc(proc::id_t pid, qkey_t key) {
        if (proc::bad_id(pid))
            return nullopt;

        qid_t next;
        // Xinu book: 'The loop must eventually terminate because the key of the tail element contains a value less than
        // the smallest valid key'
        // Keep = in <= to preserve insertion order when the priority is the same.
        // I removed it because I don't have chprio, and I want to switch to a non-idle process
        for (next = first_id(); key <= qtab[next].key; next = qtab[next].next) {
            // for (next = first_id(); key < qtab[next].key; next = qtab[next].next) {
        }

        // Insert the new element between prev and next
        auto prev = qtab[next].prev;

        qtab[pid] = { key, next, prev };

        qtab[prev].next = qtab[next].prev = pid;

        return pid;
    }

    inline optional<proc::id_t> insert_delta(proc::id_t pid, qid_t key) {
        if (proc::bad_id(pid))
            return nullopt;

        qid_t next;
        // Xinu book: 'At any time during the search, both key and qtab[next].key specify a delay relative to the time
        // at which the predecessor of "next" awakens.'
        for (next = first_id(); next != tail_id() and qtab[next].key <= key;
             key -= qtab[next].key, next = qtab[next].next) {
        }

        auto prev = qtab[next].prev;
        qtab[pid] = { key, next, prev };
        qtab[prev].next = qtab[next].prev = pid;
        if (next != tail_id())
            // Xinu book: 'The subtraction is guaranteed to produce a nonnegative value because the termination
            // condition for the loop guarantees that the key inserted is less than the next key on the list.'
            qtab[next].key -= key;

        return pid;
    }

    inline optional<proc::id_t> remove(proc::id_t pid) {
        if (proc::bad_id(pid))
            return nullopt;

        auto next = qtab[pid].next;
        auto prev = qtab[pid].prev;
        qtab[prev].next = next;
        qtab[next].prev = prev;

        return pid;
    }
};

constexpr queue_view ready_queue() { return queue_view(READY_QUEUE); }

constexpr queue_view sleep_queue() { return queue_view(SLEEP_QUEUE); }
