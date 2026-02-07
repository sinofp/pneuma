#pragma once

#include "mem/pagetable.h"

paddr_t alloc_frame();
void free_frame(paddr_t);

void map_page(pt_t &, vaddr_t, paddr_t, pte_t::flag);
void destroy_page_table(pt_t &);

namespace mem {
void init();
}

extern uint8_t __kernel_base[], __free_ram[];
extern paddr_t free_ram_end;
