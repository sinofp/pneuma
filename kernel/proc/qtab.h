#pragma once

#include "lib/optional.h"
#include "proc/proc.h"

namespace proc {

// It needs to hold prio_t or sleep_ms delta
using qkey_t = int32_t;              // Use int for minus MINKEY TODO better approach
constexpr qkey_t MINKEY = INT32_MIN; // It doesn't have to be the smallest possible value, it only needs to be smaller
                                     // than the smallest valid value

constexpr size_t NQUEUES = 2;                 // # of different queues (ready, sleep, etc.)
constexpr size_t NQENT = MAX + (NQUEUES * 2); // * 2 for head & tail
constexpr id_t READY_QUEUE = MAX;
constexpr id_t SLEEP_QUEUE = MAX + 2;

struct qent {
    qkey_t key;
    id_t next;
    id_t prev;
};

// I must declare it here for queue_view inline methods.
// I might make the methods non-inline because I don't want other source files able to access qtab directly.
extern qent qtab[];

// 'Different' queues that uses qtab
class queue_view {
    id_t q;

public:
    constexpr id_t head_id() const { return q; }

    constexpr id_t tail_id() const { return q + 1; }

    id_t first_id() const { return qtab[head_id()].next; }

    explicit constexpr queue_view(id_t id)
        : q(id) {}

    // For range-based for loop
    struct iterator {
        id_t curr;

        const qent &operator*() const { return qtab[curr]; }

        iterator &operator++() {
            curr = qtab[curr].next;
            return *this;
        }

        bool operator!=(id_t sentinel) const { return curr != sentinel; }
    };

    iterator begin() const { return iterator{ first_id() }; }

    id_t end() const { return tail_id(); }

    // This check is stircter than Xinu.
    // However, with careful code, this will do fine.
    bool empty() const { return first_id() == tail_id(); }

    // Return a reference so we can modify (for sleep queue)
    qkey_t &first_key() const { return qtab[first_id()].key; }

    inline optional<id_t> dequeue() {
        if (empty()) {
            return nullopt;
        }

        auto pid = first_id();

        auto second_id = qtab[pid].next;
        qtab[head_id()].next = second_id;
        qtab[second_id].prev = head_id();

        return pid;
    }
};

struct ready_queue_t : public queue_view {
    using queue_view::queue_view;

    inline optional<id_t> enqueue(id_t pid, qkey_t key) {
        if (bad_id(pid))
            return nullopt;

        id_t next;
        // Xinu book: 'The loop must eventually terminate because the key of the tail element contains a value less than
        // the smallest valid key'
        // Keep = in <= to preserve insertion order when the priority is the same.
        for (next = first_id(); key <= qtab[next].key; next = qtab[next].next) {
        }

        // Insert the new element between prev and next
        auto prev = qtab[next].prev;

        qtab[pid] = { key, next, prev };

        qtab[prev].next = qtab[next].prev = pid;

        return pid;
    }

    inline optional<id_t> remove(id_t pid) {
        if (bad_id(pid))
            return nullopt;

        auto next = qtab[pid].next;
        auto prev = qtab[pid].prev;
        qtab[prev].next = next;
        qtab[next].prev = prev;

        return pid;
    }
};

struct sleep_queue_t : public queue_view {
    using queue_view::queue_view;

    inline optional<id_t> enqueue(id_t pid, qkey_t key) {
        if (bad_id(pid))
            return nullopt;

        id_t next;
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
};

constexpr auto ready_queue() { return ready_queue_t(READY_QUEUE); }

constexpr auto sleep_queue() { return sleep_queue_t(SLEEP_QUEUE); }

} // namespace proc