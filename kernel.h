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
