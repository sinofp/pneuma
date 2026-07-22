#pragma once

#include "lib/types.h"

namespace sbi {
[[noreturn]]
extern void system_reset(uint32_t);
}

namespace proc {
extern void backtrace(xlen_t = 0);
} // namespace proc

namespace panic {
// Panic front half: recursion guard, IPI-freeze the other harts, hart/pid header.
// A recursive or racing panic parks the calling hart instead of returning.
extern void prologue();
// Park this hart when another hart is panicking; no-op otherwise. Checked at trap entry.
extern void park_if_panicking();
} // namespace panic

// TODO move to debug.h?
#define PANIC(fmt, ...)                                                                \
    do {                                                                               \
        panic::prologue();                                                             \
        printf_unlocked("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        proc::backtrace(reinterpret_cast<xlen_t>(__builtin_frame_address(0)));         \
        sbi::system_reset(1);                                                          \
    } while (0)
