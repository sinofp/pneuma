#pragma once

#include "lib/lock.h"
#include "lib/types.h"
#include "proc/proc.h"

namespace driver {

constexpr size_t MAX_IRQ = 96; // QEMU virt uses irqs in the low 30s; ample headroom

// Returns the pid that owns `irq`, or -1 if unowned. Used by trap dispatch.
proc::id_t owner(uint32_t irq);

// Trap-side helpers (called from the SExternalIntr handler, SIE=0, bkl held).
void mark_pending(uint32_t irq); // sets pending flag for a claimed-but-undelivered IRQ
bool wake_if_waiting(proc::process *p, uint32_t irq, lock::guard &); // true iff we actually readied p

// Syscall handlers. Return values are stuffed into trap::frame::a0 by the caller.
xlen_t sys_fdt_virtio(vaddr_t user_buf, size_t max);
xlen_t sys_mmio_map(paddr_t paddr, size_t npages);
xlen_t sys_dma_alloc(size_t npages);
xlen_t sys_irq_claim(uint32_t irq);
xlen_t sys_irq_wait(uint32_t irq);
xlen_t sys_page_walk(vaddr_t vaddr, size_t len, vaddr_t paddrs_out);

// TODO: release_owned(pid) on SYS_EXIT once drivers can exit; current drivers are long-lived.

} // namespace driver
