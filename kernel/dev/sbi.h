#pragma once

#include "lib/common.h"

namespace sbi {
void set_timer(uint64_t stime_value);
[[noreturn]]
void shutdown();
} // namespace sbi
