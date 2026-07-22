#pragma once

#include "lib/common.h"

namespace sbi {
void set_timer(uint64_t);
[[noreturn]]
void system_reset(uint32_t = 0); // 0 for shutdown, 1 for cold reboot
long hart_start(xlen_t, xlen_t, xlen_t);
void send_ipi(xlen_t, xlen_t);
} // namespace sbi
