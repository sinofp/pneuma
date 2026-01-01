#pragma once

#include "kernel.h"

paddr_t alloc_pages(size_t);

void map_page(paddr_t *, vaddr_t, paddr_t, uint32_t);

extern uint8_t __kernel_base[], __free_ram[];
extern paddr_t free_ram_end;

constexpr unsigned PAGE_V = 1 << 0; // "Valid" bit (entry is enabled)
constexpr unsigned PAGE_R = 1 << 1; // Readable
constexpr unsigned PAGE_W = 1 << 2; // Writable
constexpr unsigned PAGE_X = 1 << 3; // Executable
constexpr unsigned PAGE_U = 1 << 4; // User (accessible in user mode)
