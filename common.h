#pragma once

// Freestanding headers provided by the compiler (not libc)
#include <stddef.h> // size_t, ptrdiff_t, nullptr_t
#include <stdint.h> // uint8_t, uint32_t, uintptr_t, etc.

// 32-bit on RV32, 64-bit on RV64
using vaddr_t = uintptr_t;

#define va_list __builtin_va_list
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg

// TODO concept?
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

extern "C" {

// TODO use span
void *memcpy(void *__restrict__, const void *__restrict__, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *); // TODO replace by strlcpy
int strcmp(const char *, const char *);
void printf(const char *__restrict__, ...);

// Below functions are implemented in kernel using OpenSBI, and user using
// syscall
extern int putchar(int);
extern int getchar();
}

// Techinically, they should be xlen_t, because they need to fit in a0
// However, I don't want to expose xlen_t to user application.
// TODO enum class
constexpr unsigned long SYS_PUTCHAR = 1;
constexpr unsigned long SYS_GETCHAR = 2;
constexpr unsigned long SYS_EXIT = 3;
constexpr unsigned long SYS_READFILE = 4;
constexpr unsigned long SYS_WRITEFILE = 5;
constexpr unsigned long SYS_SLEEP = 6;

constexpr int EOF = -1;
