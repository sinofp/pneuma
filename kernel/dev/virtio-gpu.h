#pragma once

#include "dev/virtio.h"

namespace virtio::gpu {

void init();
void attach(vaddr_t fb);
void flush();

extern paddr_t base;
extern uint32_t irq;

} // namespace virtio::gpu
