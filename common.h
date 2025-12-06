#pragma once

// Freestanding headers provided by the compiler (not libc)
#include <stddef.h> // size_t, ptrdiff_t, nullptr_t
#include <stdint.h> // uint8_t, uint32_t, uintptr_t, etc.

// 32-bit on RV32, 64-bit on RV64
using vaddr_t = uintptr_t;

// Sv32 has 34-bit physical address, while Sv39 has 56-bit physical address.
// Assume we don't need more than 4GB space for RV32 with Sv32,
// otherwise all physical address arithmetic will become 64-bit on RV32.
// Additionaly, I only have hardware of RV64 with Sv39, so I care more about this configuration.
using paddr_t = uintptr_t;

// The same for both Sv32 and Sv39
constexpr size_t PAGE_SIZE = 4096;

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define va_list __builtin_va_list
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg

// TODO concept?
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

extern "C" {

void *memcpy(void *__restrict__, const void *__restrict__, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *); // TODO replace by strlcpy
int strcmp(const char *, const char *);
void printf(const char *__restrict__, ...);

// Below functions are implemented in kernel using OpenSBI, and user using
// syscall
extern void putchar(char);
extern void puts(const char *);
extern long getchar();
}

constexpr unsigned SYS_PUTCHAR = 1;
constexpr unsigned SYS_GETCHAR = 2;
constexpr unsigned SYS_EXIT = 3;
