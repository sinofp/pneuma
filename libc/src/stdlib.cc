#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

extern uint8_t __heap_start[];

namespace {

struct header_t {
    size_t size;
    int magic = 990529;
};

struct node_t {
    size_t size;
    node_t *next;
};

static_assert(sizeof(header_t) == sizeof(node_t), "node_t and header_t for free mem management is different size");

node_t *mem_list = nullptr;

} // namespace

// placement new, only for constructing a header_t in the list
inline void *operator new(size_t, void *p) noexcept { return p; }

int abs(int x) { return x < 0 ? -x : x; }

double atof(const char *str) {
    // Only used by CONFIG_VARIABLE_FLOAT(mouse_acceleration), and CONFIG_VARIABLE_FLOAT(libsamplerate_scale)
    // Won't implement if there's no problem.
    return 1;
}

int atoi(const char *str) {
    int res = 0;
    int i = 0;
    for (; ' ' == str[i]; ++i) {
    }
    int sign = 1;
    if ('-' == str[i]) {
        sign = -1;
        ++i;
    } else if ('+' == str[i]) {
        ++i;
    }
    for (; '\0' != str[i]; ++i) {
        int digit = str[i] - '0';
        if (-1 < digit and digit < 10) {
            res *= 10;
            res += digit;
        } else {
            // TODO error handling
            // Man: The function atoi() need not affect the value of errno on an error.
            // So what to do on error?
        }
    }
    return sign * res;
}

void *calloc(size_t count, size_t size) {
    auto total_size = count * size;
    auto *p = malloc(total_size);
    if (p)
        memset(p, 0, total_size);
    return p;
}

void free(void *ptr) {
    auto *hptr = reinterpret_cast<header_t *>(reinterpret_cast<uint8_t *>(ptr) - sizeof(header_t));
    if (hptr->magic != 990529) {
        // TODO error
        return;
    }

    // TODO do I need to memset 0?

    // Convert hptr to a node_t, so we can treat it the same way as other nodes.
    // I tried to convert the node_t to header_t by placement new,
    // to unify the coalesce-to-next behaviour regardless of coalesce-to-previous passed or not.
    // It wasn't correct because header_t::magic corrupts the node_t.
    auto *new_node = new (hptr) node_t{ sizeof(header_t) + hptr->size - sizeof(node_t) };

    node_t *curr = mem_list, *prev = nullptr;
    for (; curr; prev = curr, curr = curr->next) {
        if (new_node < curr)
            break;
    }

    if (prev) {
        auto *end_addr = reinterpret_cast<uint8_t *>(prev) + sizeof(node_t) + prev->size;
        if (end_addr == reinterpret_cast<decltype(end_addr)>(new_node)) {
            // Coalesce to previous
            prev->size += new_node->size + sizeof(node_t);
            new_node = prev; // Willl be used in coalesce-to-next
        } else {
            // Insert as new node
            prev->next = new_node;
            new_node->next = curr;
        }
    } else {
        // Insert as new node TODO start prev from mem_list, and curr from mem_list->next to simplify branching
        mem_list = new_node;
        new_node->next = curr;
    }

    auto *end_addr = reinterpret_cast<uint8_t *>(new_node) + sizeof(node_t) + new_node->size;
    if (end_addr == reinterpret_cast<decltype(end_addr)>(curr)) {
        // Coalesce to next
        new_node->size += curr->size + sizeof(node_t);
        // new_node is already inserted, so no prev->next
        new_node->next = curr->next;
    }
}

void *malloc(size_t size) {
    if (!mem_list) {
        mem_list = reinterpret_cast<node_t *>(__heap_start);
        // TODO should I make node_t::size to mean the whole block size AFTER removing the node?
        mem_list->size = 8 * 1024 * 1024 - sizeof(node_t);
        mem_list->next = nullptr;
        // |node_t|<------- node size --------->|
    }
    auto total_size = sizeof(header_t) + size;

    node_t *curr, *prev = nullptr;
    // First-fit, because it's easy
    for (curr = mem_list; curr; prev = curr, curr = curr->next) {
        if (total_size < curr->size) {
            // Split if we have the space without using the space of node
            // v curr                       v header
            // |node_t|<------- node size --------------------------->|
            // |node_t|<-- new node size -->|header_t|<-- req size -->|
            curr->size -= total_size;
            void *addr = reinterpret_cast<uint8_t *>(curr) + sizeof(node_t) + curr->size;
            auto *header = new (addr) header_t{ size };
            return reinterpret_cast<uint8_t *>(header) + sizeof(header_t);
        } else if (total_size <= curr->size + sizeof(node_t)) {
            // Remove the node if we have to use the space of node to fulfill the request
            // v curr/header
            // |node_t|<------- node size --------------------------->|
            // |header_t|<----- req size ---------------------->|waste|
            //          |<----- curr->size + node_t - header_t ------>|
            // In fact, node_t and header_t is the same size, see the static_assert
            (prev ? prev->next : mem_list) = curr->next;
            auto *header = new (curr) header_t{ curr->size + sizeof(node_t) - sizeof(header_t) };
            return reinterpret_cast<uint8_t *>(header) + sizeof(header_t);
        }
    }
    errno = ENOMEM;
    return NULL;
}

void *realloc(void *ptr, size_t size) {
    if (NULL == ptr)
        return malloc(size);
    // Man: If size is zero and ptr is not NULL, a new,
    // minimum sized object is allocated and the original object is freed.
    // What does it mean?
    if (!size) {
        free(ptr);
        return malloc(1);
    }
    // TODO smarter expansion method
    if (auto new_p = malloc(size)) {
        auto *hptr = reinterpret_cast<header_t *>(reinterpret_cast<uint8_t *>(ptr) - sizeof(header_t));
        if (hptr->magic != 990529) {
            // TODO error
            return NULL;
        }
        memcpy(new_p, ptr, hptr->size < size ? hptr->size : size);
        free(ptr); // I nearly forgot this
        return new_p;
    }
    // errno is already set to ENOMEM by malloc, if malloc returns NULL
    return NULL;
}

char *getenv(const char *name) {
    // Can only be DOOMWADPATH, DOOMWADDIR, TEMP
    return "hahaha";
}

int remove(const char *path) {
    // TODO syscall
    return 0;
}

int rename(const char *old, const char *_new) {
    // TODO syscall
    return 0;
}

[[noreturn]]
void exit(int status /*status is ignored for now*/) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;)
        ; // Just in case!
}
