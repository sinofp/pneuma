#pragma once

#include "dev/virtio.h"

namespace virtio::input {

struct [[gnu::packed]] event {
    le16 type;
    le16 code;
    le32 value;
};

void init();
void handle_interrupt();
bool dequeue(event &out);

extern paddr_t base;
extern uint32_t irq;

} // namespace virtio::input
