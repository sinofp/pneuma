#pragma once

#include "kernel.h"

namespace fdt {

constexpr uint32_t uninitialised = 0x990529;

struct header;

void init(const header *fdt_ptr);

} // namespace fdt
