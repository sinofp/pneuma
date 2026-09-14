#pragma once

#include "lib/lock.h"
#include "lib/types.h"
#include "proc/proc.h"
#include <shared.h>

namespace driver {

constexpr size_t MAX_IRQ = 0x90; // VF2 has 1-0x88

// Returns the pid that owns `irq`, or -1 if unowned. Used by trap dispatch.
proc::id_t owner(uint32_t irq);

// Trap-side helpers (called from the SExternalIntr handler, SIE=0, bkl held).
void mark_pending(uint32_t irq);                                // sets pending flag for a claimed-but-undelivered IRQ
bool wake_if_waiting(proc::process *, uint32_t, lock::guard &); // true iff we actually readied p

// Syscall handlers. Return values are stuffed into trap::frame::a0 by the caller.
xlen_t sys_fdt_query(const char *, vaddr_t, size_t);
xlen_t sys_mmio_map(paddr_t, size_t);
xlen_t sys_dma_alloc(size_t);
xlen_t sys_irq_claim(uint32_t);
xlen_t sys_irq_wait(uint32_t);
xlen_t sys_page_walk(vaddr_t, size_t, vaddr_t);

// TODO: release_owned(pid) on SYS_EXIT once drivers can exit; current drivers are long-lived.

} // namespace driver
