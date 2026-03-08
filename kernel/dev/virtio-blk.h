#pragma once

#include "dev/virtio.h"

namespace virtio::blk {

void init();
void read_write(void *buf, uint64_t sector, int is_write);
void handle_io_completion();

extern paddr_t vq_paddr; // allocated by mem::init, consumed by virtq_init
extern uint64_t capacity;
extern paddr_t base;
extern uint32_t irq;

} // namespace virtio::blk
