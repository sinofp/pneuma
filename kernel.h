#pragma once
#include "common.h"

#define PANIC(fmt, ...)                                                       \
    do {                                                                      \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        while (1) {                                                           \
        }                                                                     \
    } while (0)

#if __riscv_xlen == 32
#define STORE "sw"
#define LOAD "lw"
#else
#define STORE "sd"
#define LOAD "ld"
#endif

// Or uintptr_t.
// I choose unsigned long because of its usage in OpenSBI interfaces
using xlen_t = unsigned long;

// Sv32 has 34-bit physical address, while Sv39 has 56-bit physical address.
// Assume we don't need more than 4GB space for RV32 with Sv32,
// otherwise all physical address arithmetic will become 64-bit on RV32.
// Additionaly, I only have hardware of RV64 with Sv39, so I care more about this configuration.
using paddr_t = uintptr_t;

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

// The same for both Sv32 and Sv39
constexpr size_t PAGE_SIZE = 4096;

namespace mmio {
// TODO concept to restrict T to uint32_t and uint64_t
template <typename T> inline auto read(paddr_t addr) { return *reinterpret_cast<volatile T *>(addr); }

template <typename T> inline auto read(paddr_t base, size_t offset) { return read<T>(base + offset); }

template <typename T> inline void write(paddr_t addr, T data) { *reinterpret_cast<volatile T *>(addr) = data; }

template <typename T> inline void write(paddr_t base, size_t offset, T data) { return write(base + offset, data); }
} // namespace mmio

extern uint32_t clk_freq_ms;
extern uint32_t clk_freq_us;
