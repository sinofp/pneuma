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

xlen_t sys_irq_claim(uint32_t irq) {
    ensure_init();
    if (irq >= MAX_IRQ)
        return static_cast<xlen_t>(-1);

    csr::intr_guard ig;

    if (-1 != owners[irq])
        return static_cast<xlen_t>(-1);
    owners[irq] = proc::current->pid;
    plic::enable_irq(irq);
    // Drivers must claim before their first device kick. QEMU's PLIC re-evaluates delivery
    // on line edges and claim/complete writes, not on enable writes, so enabling after the
    // device already asserted the line loses the interrupt forever.
    return 0;
}

xlen_t sys_irq_wait(uint32_t irq) {
    ensure_init();
    if (irq >= MAX_IRQ)
        return static_cast<xlen_t>(-1);

    csr::intr_guard g;

    if (owners[irq] != proc::current->pid)
        return static_cast<xlen_t>(-1);
    plic::complete(0, irq); // ack the prior cycle so PLIC can deliver the next one (no-op right after claim)

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

xlen_t sys_page_walk(vaddr_t vaddr, size_t len, vaddr_t paddrs_out) {
    // Emit one page-aligned paddr per page covered by [vaddr, vaddr+len). The caller reconstructs
    // the head offset (vaddr & (PAGE_SIZE-1)) and tail length itself — the kernel just reports frames.
    auto *out = reinterpret_cast<uint64_t *>(paddrs_out);
    size_t n = 0;
    auto page_mask = static_cast<vaddr_t>(mem::PAGE_SIZE - 1);
    for (vaddr_t v = vaddr & ~page_mask, end = vaddr + len; v < end; v += mem::PAGE_SIZE)
        out[n++] = static_cast<uint64_t>(mem::to_paddr(proc::current->page_table, v));
    return static_cast<xlen_t>(n);
}

} // namespace driver
