#pragma once

#include "lib/span.h"
#include "lib/types.h"
#include "shared.h"

namespace fdt {

constexpr uint32_t uninitialised = 0x990529;

struct header;

void init(const header *fdt_ptr);

constexpr size_t MAX_DEVS = 9;
extern size_t dev_cnt;

struct fdt_dev_entry {
    fdt_dev_t dev{};
    span<const char> compatible;
};

extern fdt_dev_entry devs[MAX_DEVS];

bool is_compatible(const fdt_dev_entry &, const char *target);

extern bool has_gpu;

extern uint32_t clk_freq_ms;

} // namespace fdt
