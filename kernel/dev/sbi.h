#pragma once

#include "lib/common.h"

void sbi_set_timer(uint64_t stime_value);

[[noreturn]]
void shutdown();
