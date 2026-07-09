#pragma once

#include "lib/types.h"

#define va_list                  __builtin_va_list
#define va_start                 __builtin_va_start
#define va_end                   __builtin_va_end
#define va_arg                   __builtin_va_arg

#define align_up(value, align)   __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

// TODO concept?
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

extern "C" {

// TODO use span
void *memcpy(void *__restrict__, const void *__restrict__, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *); // TODO replace by strlcpy
int strcmp(const char *, const char *);
size_t strlen(const char *);
void printf(const char *__restrict__, ...);
void printf_unlocked(const char *__restrict__, ...);

// Below functions are implemented in kernel using OpenSBI, and user using
// syscall
extern int putchar(int);
extern int getchar();
}

// Techinically, they should be xlen_t, because they need to fit in a0
// However, I don't want to expose xlen_t to user application.
// TODO enum class
// NOTE shuold match libc/include/sys/syscall.h
constexpr unsigned long SYS_PUTCHAR = 1;
constexpr unsigned long SYS_GETCHAR = 2;
constexpr unsigned long SYS_EXIT = 3;
constexpr unsigned long SYS_SLEEP = 6;
constexpr unsigned long SYS_GET_TICKS = 8;
constexpr unsigned long SYS_MP_SSEND = 12;
constexpr unsigned long SYS_MP_RECV = 13;
constexpr unsigned long SYS_FDT_VIRTIO = 14;
constexpr unsigned long SYS_MMIO_MAP = 15;
constexpr unsigned long SYS_DMA_ALLOC = 16;
constexpr unsigned long SYS_IRQ_WAIT = 17;
constexpr unsigned long SYS_PAGE_WALK = 18;
constexpr unsigned long SYS_IRQ_CLAIM = 19;

constexpr int EOF = -1;

namespace sbi {
[[noreturn]]
extern void shutdown();
}

namespace proc {
extern void backtrace();
}

// TODO move to debug.h?
#define PANIC(fmt, ...)                                                                \
    do {                                                                               \
        printf_unlocked("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        proc::backtrace();                                                             \
        sbi::shutdown();                                                               \
    } while (0)
