#pragma once

#include "mem/pagetable.h"
#include "lib/lock.h"

namespace mem {
void init();

paddr_t alloc_frame();
void free_frame(paddr_t);

void map_page(pt_t &, vaddr_t, paddr_t, pte_t::flag);
void destroy_page_table(pt_t &);

paddr_t to_paddr(pt_t &, vaddr_t);
void cross_proc_memcpy(pt_t &dst_pt, vaddr_t dst, pt_t &src_pt, vaddr_t src, size_t len);

inline void for_page_seg(vaddr_t vaddr, size_t len, auto f)
    requires requires(vaddr_t v, size_t l) { f(v, l); }
{
    size_t seg;
    for (; 0 < len; vaddr += seg, len -= seg) {
        seg = min(len, PAGE_SIZE - (vaddr & (PAGE_SIZE - 1)));
        f(vaddr, seg);
    }
}
} // namespace mem

extern uint8_t __kernel_base[], __free_ram[];
extern paddr_t free_ram_end;
