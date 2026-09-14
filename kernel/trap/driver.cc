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

bool wake_if_waiting(proc::process *p, uint32_t irq, lock::guard &g) {
    // Return value lets the caller reschedule only when we actually made someone ready —
    // otherwise the SExternalIntr trap would waste a no-op reschedule() on every stray IRQ.
    // When we do wake, the caller must reschedule promptly: without it the driver would sit
    // in the ready queue until the next 5 ms preempt tick, dominating IPC-bound disk I/O latency.
    if (proc::state_t::SUSPENDED != p->state or p->awaited_irq != irq)
        return false;
    proc::ready(p, g);
    return true;
}

xlen_t sys_fdt_query(const char *compatible, vaddr_t user_buf, size_t max) {
    // TODO check user_buf has U bit.
    if (user_buf < proc::USER_BASE or reinterpret_cast<vaddr_t>(__kernel_base) <= user_buf)
        PANIC("Strange user_buf in fdt_query");

    // Assumes user_buf is an array in one page
    auto *dst = reinterpret_cast<fdt_dev_t *>(user_buf);
    xlen_t j = 0;
    for (size_t i = 0; i < fdt::dev_cnt and j < max; ++i)
        // Both User and Kernel is mapped, therefore it's safe to use the string pointer
        if (const auto &src = fdt::devs[i]; fdt::is_compatible(src, compatible))
            dst[j++] = src.dev;
    return j;
}

xlen_t sys_mmio_map(paddr_t paddr, size_t npages) {
    if (!is_aligned(paddr, mem::PAGE_SIZE))
        return 0;

    // I got a npages==526808 for an out-of-bound fdt_dev_t
    if (512 < npages)
        return 0;

    paddr_t end = paddr + npages * mem::PAGE_SIZE;
    // overflow
    if (end < paddr)
        return 0;

    // So the user can't map kernel code.
    bool invalid = true;
    for (size_t i = 0; i < fdt::dev_cnt; ++i)
        if (const auto &dev = fdt::devs[i].dev;
            dev.mmio_base <= paddr and end <= align_up(dev.mmio_base + dev.mmio_size, mem::PAGE_SIZE) or
            dev.device_id and dev.vq_paddr == paddr and npages == virtio::virtq_pages) {
            invalid = false;
            break;
        }
    if (invalid)
        return 0; // TODO more error code?

    csr::intr_guard ig;
    lock::guard g{ lock::bkl }; // map_page may alloc_frame for intermediate tables
    using enum mem::pte_t::flag;
    for (size_t i = 0; i < npages; i++) {
        auto offset = i * mem::PAGE_SIZE;
        mem::map_page(proc::current().page_table, paddr + offset, paddr + offset, U | R | W);
    }
    tlb_flush();
    // Identity-mapped: vaddr == paddr
    return static_cast<xlen_t>(paddr);
}

xlen_t sys_dma_alloc(size_t npages) {
    if (1 != npages) [[unlikely]]
        PANIC("sys_dma_alloc: only npages=1 supported");
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    using enum mem::pte_t::flag;
    auto paddr = mem::alloc_frame();
    mem::map_page(proc::current().page_table, paddr, paddr, U | R | W);
    tlb_flush();
    return static_cast<xlen_t>(paddr);
}

xlen_t sys_irq_claim(uint32_t irq) {
    ensure_init();
    if (irq >= MAX_IRQ)
        return static_cast<xlen_t>(-1);

    csr::intr_guard ig;
    lock::guard g{ lock::bkl };

    if (-1 != owners[irq])
        return static_cast<xlen_t>(-1);
    owners[irq] = proc::current().pid;
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

    csr::intr_guard ig;
    lock::guard g{ lock::bkl };

    if (owners[irq] != proc::current().pid)
        return static_cast<xlen_t>(-1);
    plic::complete(irq); // ack the prior cycle so PLIC can deliver the next one (no-op right after claim)

    if (pendings[irq]) {
        pendings[irq] = false;
        return 0;
    }

    proc::current().awaited_irq = irq;
    proc::current().state = proc::state_t::SUSPENDED;
    proc::reschedule(g);

    // Resumed after the IRQ fired; the trap handler set pendings[irq]. reschedule released
    // the bkl inside switch_context, so consuming the mark needs a fresh acquisition.
    {
        lock::guard g2{ lock::bkl };
        pendings[irq] = false;
    }
    return 0;
}

xlen_t sys_page_walk(vaddr_t vaddr, size_t len, vaddr_t paddrs_out) {
    // Emit one page-aligned paddr per page covered by [vaddr, vaddr+len). The caller reconstructs
    // the head offset (vaddr & (PAGE_SIZE-1)) and tail length itself — the kernel just reports frames.
    auto *out = reinterpret_cast<uint64_t *>(paddrs_out);
    size_t n = 0;
    auto page_mask = static_cast<vaddr_t>(mem::PAGE_SIZE - 1);
    for (vaddr_t v = vaddr & ~page_mask, end = vaddr + len; v < end; v += mem::PAGE_SIZE)
        out[n++] = static_cast<uint64_t>(mem::to_paddr(proc::current().page_table, v));
    return static_cast<xlen_t>(n);
}

} // namespace driver
