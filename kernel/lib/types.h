#pragma once

// Freestanding headers provided by the compiler (not libc)
#include <stddef.h> // size_t, ptrdiff_t, nullptr_t
#include <stdint.h> // uint8_t, uint32_t, uintptr_t, etc.

// 32-bit on RV32, 64-bit on RV64
using vaddr_t = uintptr_t;

// Or uintptr_t.
// I choose unsigned long because of its usage in OpenSBI interfaces
using xlen_t = unsigned long;

using sxlen_t = unsigned long;

// Sv32 has 34-bit physical address, while Sv39 has 56-bit physical address.
// Assume we don't need more than 4GB space for RV32 with Sv32,
// otherwise all physical address arithmetic will become 64-bit on RV32.
// Additionaly, I only have hardware of RV64 with Sv39, so I care more about this configuration.
using paddr_t = uintptr_t;

// Architecture-specific load/store instructions for inline assembly
#if __riscv_xlen == 32
#define STORE "sw"
#define LOAD "lw"
#else
#define STORE "sd"
#define LOAD "ld"
#endif
