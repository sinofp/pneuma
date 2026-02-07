#include "mem/mem.h"
#include "dev/virtio.h"
#include "lib/span.h"

namespace {
// Everything [bump_paddr, free_ram_end) will be managed by free stack
auto bump_paddr = reinterpret_cast<paddr_t>(__free_ram);

// Allocate contiguous physical pages.
// To simplify the management of free page frames, I made alloc_frame only capable of allocating one frame.
// Only this alloc_pages can allocate contiguous physical pages
// (or page frame, it doesn't matter because virtual addressing hasn't been enabled, and later this region will be
// identical-mapped). Therefore I put it here and allocate any memory that requires a big physical region before
// enabling virtual addresses.
paddr_t alloc_pages(size_t n) {
    if (n <= 1) [[unlikely]]
        PANIC("alloc_pages requires 1 < n. Any requests where 1 == n should wait and use alloc_frame");

    auto paddr = bump_paddr;
    bump_paddr += n * PAGE_SIZE;

    if (free_ram_end < bump_paddr) [[unlikely]]
        PANIC("alloc_pages: out of memory, or called after mem::init");

    memset(reinterpret_cast<void *>(paddr), 0, n * PAGE_SIZE);
    return paddr;
}

class frame_stack {
    span<paddr_t> storage;
    size_t count;

public:
    // We use an empty span to constructor the object, then replace it after we have the correct backing span.
    // This way, we can declare (allocate) a frame_stack object in .data normally.
    frame_stack()
        : storage({ nullptr, 0 })
        , count(0) {}

    // TODO replace by default ctor and move?
    void reset(span<paddr_t> space) {
        storage = space;
        count = 0;
    }

    size_t capacity() const { return storage.size(); }

    void push(paddr_t value) {
        if (capacity() <= count) [[unlikely]]
            PANIC("free_stack exceeds free stack capacity");
        storage.data()[count++] = value;
    }

    paddr_t pop() {
        if (count == 0) [[unlikely]]
            PANIC("free_stack::pop: out of memory");
        return storage.data()[--count];
    }
};

frame_stack free_stack;
} // namespace

namespace mem {
void init() {
    // The virtq requiers more than one contiguous physical pages,
    // while our alloc_frame + map_page is designed to allocate contiguous virtual pages.
    // To forbid the usage of alloc_pages after we initilise the free stack, we allocate the virtq here.
    blk_vq_paddr = alloc_pages(align_up(sizeof(virtio_virtq), PAGE_SIZE) / PAGE_SIZE);

    auto base = bump_paddr;
    auto total_bytes = free_ram_end - base;

    // Get the capacity of free_stack
    size_t pages = total_bytes / PAGE_SIZE, stack_bytes;
    // printf("The initial estimation on free_stack capacity is 0x%x\n", pages);
    for (size_t new_pages = 0; pages != new_pages; pages = new_pages) {
        stack_bytes = align_up(pages * sizeof(paddr_t), PAGE_SIZE);
        new_pages = (total_bytes - stack_bytes) / PAGE_SIZE;
        // printf("The new estimation on free_stack capacity is 0x%x\n", new_pages);
    }

    free_stack.reset({ reinterpret_cast<paddr_t *>(base), pages });
    auto page_base = base + stack_bytes;
    for (size_t i = 0; i < pages; ++i)
        free_stack.push(page_base + i * PAGE_SIZE);

    // Disable alloc_pages TODO remove?
    bump_paddr = free_ram_end;
}
} // namespace mem

paddr_t alloc_frame() {
    if (!free_stack.capacity()) [[unlikely]]
        PANIC("alloc_frame called before mem::init");

    auto paddr = free_stack.pop();
    memset(reinterpret_cast<void *>(paddr), 0, PAGE_SIZE);
    return paddr;
}

void free_frame(paddr_t paddr) {
    if (!is_aligned(paddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned paddr 0x%lx", paddr);

    if (!free_stack.capacity()) [[unlikely]]
        PANIC("free_frame called before mem::init");

    free_stack.push(paddr);
}

void map_page(pt_t &root_table, vaddr_t vaddr, paddr_t paddr, pte_t::flag flags) {
    if (!is_aligned(vaddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned vaddr %p", reinterpret_cast<void *>(vaddr));

    if (!is_aligned(paddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned paddr 0x%lx", paddr);

    // This should be faster with branches in vpn function
    size_t vpn_arr[PAGE_LEVELS];
    if constexpr (__riscv_xlen == 32) {
        vpn_arr[0] = vpn<0>(vaddr);
        vpn_arr[1] = vpn<1>(vaddr);
    } else {
        vpn_arr[0] = vpn<0>(vaddr);
        vpn_arr[1] = vpn<1>(vaddr);
        vpn_arr[2] = vpn<2>(vaddr);
    }

    auto map_level = [&](this auto &&self, pt_t &table, int level) -> void {
        size_t offset = vpn_arr[level];

        if (level == 0) {
            table[offset] = pte_t::make_leaf(paddr, flags);
            return;
        }

        if (!table[offset].is_valid())
            // Create intermediate page table if it doesn't exist
            table[offset] = pte_t::make_nonleaf(alloc_frame());

        pt_t next_table{ table[offset] };
        self(next_table, level - 1);
    };

    map_level(root_table, PAGE_LEVELS - 1);
}

void destroy_page_table(pt_t &root_table) {
    auto free_level = [=](this auto &&self, pt_t &table) -> void {
        for (size_t i = 0; i < pt_t::size; ++i) {
            auto pte = table[i];
            if (!pte.is_valid())
                continue;
            if (pte.is_leaf()) {
                if (pte.is_user())
                    free_frame(pte.paddr());
                continue;
            }

            auto next = pt_t{ pte };
            self(next);
            free_frame(next);
        }
    };

    free_level(root_table);
    free_frame(root_table);
}
