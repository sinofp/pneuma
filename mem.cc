#include "mem.h"

extern uint8_t __free_ram[], __free_ram_end[];

paddr_t alloc_pages(size_t n) {
    static auto next_paddr = reinterpret_cast<paddr_t>(__free_ram);
    auto paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > reinterpret_cast<paddr_t>(__free_ram_end)) [[unlikely]]
        PANIC("out of memory");

    memset(reinterpret_cast<void *>(paddr), 0, n * PAGE_SIZE);
    return paddr;
}

void map_page(paddr_t *page_table, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned vaddr %p", reinterpret_cast<void *>(vaddr));

    if (!is_aligned(paddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned paddr 0x%lx", paddr);

    // TODO recurisive?

    if constexpr (__riscv_xlen == 32) {
        // Sv32: 2-level page table, 10-bit VPNs
        auto *table1 = page_table;
        const xlen_t vpn1 = vaddr >> 22;           // [31:22]
        const xlen_t vpn0 = (vaddr >> 12) & 0x3ff; // [21:12]

        xlen_t pte = table1[vpn1];
        if (!(pte & PAGE_V)) {
            // Create the first level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table1[vpn1] = pte = ppn << 10 | PAGE_V;
        }

        xlen_t ppn = pte >> 10;
        auto *const table0 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        ppn = paddr / PAGE_SIZE; // == >> 12, why waste the last two bits of PTE?
        pte = ppn << 10 | flags | PAGE_V;
        table0[vpn0] = pte;
    } else {
        // Sv39: 3-level page table, 9-bit VPNs
        auto *table2 = page_table;
        const xlen_t vpn2 = (vaddr >> 30) & 0x1ff; // Ignore sign extension [63:39], keep [38:30]
        const xlen_t vpn1 = (vaddr >> 21) & 0x1ff; // [29:21]
        const xlen_t vpn0 = (vaddr >> 12) & 0x1ff; // [20:12]

        xlen_t pte = table2[vpn2];
        if (!(pte & PAGE_V)) {
            // Create the second level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table2[vpn2] = pte = ppn << 10 | PAGE_V; // No need to sign ext for pte
        }

        xlen_t ppn = (pte >> 10) & 0xfff'ffff'ffff; // Ignore reserved [63:54], keep [53:10]
        auto *const table1 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        pte = table1[vpn1];
        if (!(pte & PAGE_V)) {
            // Create the first level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table1[vpn1] = pte = ppn << 10 | PAGE_V; // No need to sign ext for pte
        }

        ppn = (pte >> 10) & 0xfff'ffff'ffff; // Ignore reserved [63:54], keep [53:10]
        auto *const table0 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        ppn = paddr / PAGE_SIZE;
        pte = ppn << 10 | flags | PAGE_V;
        table0[vpn0] = pte;
    }
}
