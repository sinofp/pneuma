#pragma once

#include "lib/types.h"

namespace mmio {
// TODO concept to restrict T to uint32_t and uint64_t
// TODO delete single argument version?
template <typename T> inline auto read(paddr_t addr) { return *reinterpret_cast<volatile T *>(addr); }

template <typename T> inline auto read(paddr_t base, size_t offset) { return read<T>(base + offset); }

template <typename T> inline void write(paddr_t addr, T data) { *reinterpret_cast<volatile T *>(addr) = data; }

template <typename T> inline void write(paddr_t base, size_t offset, T data) { return write(base + offset, data); }

template <typename T> inline void fetch_and_or(paddr_t base, size_t offset, T data) {
    auto addr = base + offset;
    write<T>(addr, read<T>(addr) | data);
}

} // namespace mmio
