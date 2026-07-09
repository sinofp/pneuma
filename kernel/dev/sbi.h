#pragma once

#include "lib/common.h"

namespace sbi {
void set_timer(uint64_t);
[[noreturn]]
void shutdown();
long hart_start(xlen_t, xlen_t, xlen_t);
void send_ipi(xlen_t, xlen_t);
} // namespace sbi
