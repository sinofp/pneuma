#pragma once

#include "lib/common.h"
#include "lib/span.h"

namespace mem {

// The same for both Sv32 and Sv39
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t PAGE_SHIFT = 12;
constexpr size_t PTE_FLAG_BITS = 10;

// Number of page table levels: Sv32 has 2 levels, Sv39 has 3 levels
constexpr unsigned PAGE_LEVELS = __riscv_xlen == 32 ? 2 : 3;

class pte_t {
    xlen_t raw = 0;

    constexpr auto ppn() const { return raw >> PTE_FLAG_BITS; }

public:
    enum class flag : xlen_t {
        V = 1 << 0, // "Valid" bit (entry is enabled)
        R = 1 << 1, // Readable
        W = 1 << 2, // Writable
        X = 1 << 3, // Executable
        U = 1 << 4, // User (accessible in user mode)
    };

    friend constexpr flag operator|(flag a, flag b) {
        return static_cast<flag>(static_cast<xlen_t>(a) | static_cast<xlen_t>(b));
    }

    constexpr pte_t() = default;

    constexpr explicit pte_t(xlen_t raw_value)
        : raw(raw_value) {}

    constexpr bool is_valid() const { return (raw & static_cast<xlen_t>(flag::V)) != 0; }

    constexpr bool is_leaf() const { return (raw & static_cast<xlen_t>(flag::R | flag::W | flag::X)) != 0; }

    constexpr bool is_user() const { return (raw & static_cast<xlen_t>(flag::U)) != 0; }

    // Only used to assign to a xlen_t
    constexpr operator xlen_t() const { return raw; }

    // Not operator paddr_t() because the raw is not paddr_t
    constexpr paddr_t paddr() const {
        if constexpr (__riscv_xlen == 32)
            return static_cast<paddr_t>(ppn()) * PAGE_SIZE;
        // Ignore reserved [63:54], keep [53:PTE_FLAG_BITS]
        return static_cast<paddr_t>(ppn() & 0xfff'ffff'ffffull) * PAGE_SIZE;
    }

    // Used to be named make_table, but it doesn't return a pt_t, so make_nonleaf
    static constexpr auto make_nonleaf(paddr_t paddr) {
        return pte_t{ (paddr / PAGE_SIZE) << PTE_FLAG_BITS | static_cast<xlen_t>(flag::V) };
    }

    static constexpr auto make_leaf(paddr_t paddr, flag flags) {
        return pte_t{ (paddr / PAGE_SIZE) << PTE_FLAG_BITS | static_cast<xlen_t>(flags | flag::V) };
    }
};

class pt_t {
    span<pte_t> table;

public:
    static constexpr size_t size = PAGE_SIZE * 8 / __riscv_xlen;

    // Used for an empty field in struct process
    constexpr pt_t()
        : table{ nullptr, size } {}

    // Only used in proc.cc to create the root table, therefore no checks
    explicit constexpr pt_t(paddr_t base)
        : table{ reinterpret_cast<pte_t *>(base), size } {}

    explicit constexpr pt_t(pte_t pte)
        : table{ reinterpret_cast<pte_t *>(pte.paddr()), size } {
        if (!pte.is_valid()) [[unlikely]]
            PANIC("pt_t: PTE not valid");
        if (pte.is_leaf()) [[unlikely]]
            PANIC("pt_t: PTE is leaf, not table");
    }

    constexpr auto &operator[](size_t index) { return table.data()[index]; }

    constexpr const auto &operator[](size_t index) const { return table.data()[index]; }

    constexpr operator paddr_t() const { return reinterpret_cast<paddr_t>(table.data()); }
};

// TODO create a paddr_t class?
template <unsigned L> constexpr xlen_t vpn(vaddr_t vaddr) {
    // This must be outside if constexpr because templates are instantiated even in discarded branches
    static_assert(L < 3, "vpn supports levels 2, 1, and 0 at most");
    if constexpr (__riscv_xlen == 32) {
        if constexpr (L == 1)
            return vaddr >> 22;               // [31:22]
        return (vaddr >> PAGE_SHIFT) & 0x3ff; // [21:PAGE_SHIFT]
    } else {
        if constexpr (L == 2)
            return (vaddr >> 30) & 0x1ff; // Ignore sign extension [63:39], keep [38:30]
        if constexpr (L == 1)
            return (vaddr >> 21) & 0x1ff;     // [29:21]
        return (vaddr >> PAGE_SHIFT) & 0x1ff; // [20:PAGE_SHIFT]
    }
}
} // namespace mem
