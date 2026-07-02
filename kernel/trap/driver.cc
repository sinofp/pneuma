#include "trap/driver.h"
#include "fdt/fdt.h"
#include "lib/common.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "trap/csr.h"
#include "trap/plic.h"
#include <virtio.h>

namespace {

proc::id_t owners[driver::MAX_IRQ];
bool pendings[driver::MAX_IRQ];

bool initialised = false;

void ensure_init() {
    if (initialised)
        return;
    for (auto &o : owners)
        o = -1;
    initialised = true;
}

inline void tlb_flush() {
    asm volatile("sfence.vma" ::
                     : "memory");
}

} // namespace

namespace driver {

proc::id_t owner(uint32_t irq) {
    ensure_init();
    if (irq >= MAX_IRQ)
        return -1;
    return owners[irq];
}

void mark_pending(uint32_t irq) {
    if (irq < MAX_IRQ)
        pendings[irq] = true;
}

void wake_if_waiting(proc::process *p, uint32_t irq) {
    if (proc::state_t::SUSPENDED == p->state and p->awaited_irq == irq)
        // instant=true so the IRQ trap immediately reschedules to the (typically
        // higher-priority) driver — otherwise the driver waits up to one scheduler
        // quantum (5 ms) before running, dominating IPC-bound disk I/O latency.
        proc::ready(p, true);
}

xlen_t sys_fdt_virtio(vaddr_t user_buf, size_t max) {
    auto count = min(max, fdt::virtio_device_count);
    auto *out = reinterpret_cast<virtio::virtio_dev_t *>(user_buf);
    for (size_t i = 0; i < count; i++) {
        const auto &src = fdt::virtio_devices[i];
        out[i].mmio_base = src.mmio_base;
        out[i].vq_paddr = src.vq_paddr;
        out[i].irq = src.irq;
        out[i].device_id = src.device_id;
    }
    return count;
}

xlen_t sys_mmio_map(paddr_t paddr, size_t npages) {
    if (!is_aligned(paddr, mem::PAGE_SIZE))
        return 0;
    using enum mem::pte_t::flag;
    for (size_t i = 0; i < npages; i++) {
        auto offset = i * mem::PAGE_SIZE;
        mem::map_page(proc::current->page_table, paddr + offset, paddr + offset, U | R | W);
    }
    tlb_flush();
    // Identity-mapped: vaddr == paddr
    return static_cast<xlen_t>(paddr);
}

xlen_t sys_dma_alloc(size_t npages) {
    if (1 != npages) [[unlikely]]
        PANIC("sys_dma_alloc: only npages=1 supported");
    using enum mem::pte_t::flag;
    auto paddr = mem::alloc_frame();
    mem::map_page(proc::current->page_table, paddr, paddr, U | R | W);
    tlb_flush();
    return static_cast<xlen_t>(paddr);
}

xlen_t sys_irq_wait(uint32_t irq) {
    ensure_init();
    if (irq >= MAX_IRQ)
        return static_cast<xlen_t>(-1);

    csr::intr_guard g;

    auto pid = proc::current->pid;
    if (-1 == owners[irq]) {
        owners[irq] = pid;
        plic::enable_irq(irq);
        // First call: no prior IRQ to complete.
    } else if (owners[irq] != pid) {
        return static_cast<xlen_t>(-1);
    } else {
        plic::complete(0, irq); // ack the prior cycle so PLIC can deliver the next one
    }

    if (pendings[irq]) {
        pendings[irq] = false;
        return 0;
    }

    proc::current->awaited_irq = irq;
    proc::current->state = proc::state_t::SUSPENDED;
    proc::reschedule();

    // Resumed after IRQ fired; trap handler set pendings[irq].
    pendings[irq] = false;
    return 0;
}

} // namespace driver
