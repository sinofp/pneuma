#pragma once

#include "kernel.h"

namespace csr {

using sxlen_t = unsigned long;
using V = sxlen_t;

template <typename T, typename E> class reg {

protected:
    V value;

    explicit constexpr reg(V v)
        : value(v) {}

public:
    // This is not needed because we only have one field, and we don't have vtbl.
    // I just want to be explicit.
    constexpr operator V() const { return value; }

    constexpr E encoding() const { return static_cast<E>(value); }

    auto set(E bitfield) const { return T{ value | static_cast<V>(bitfield) }; }

    auto unset(E bitfield) const { return T{ value & ~static_cast<V>(bitfield) }; }
};

constexpr sxlen_t MSB = 1ull << (__riscv_xlen - 1);

enum class scause_enc : sxlen_t {
    // Exceptions
    InstAddrMisaligned = 0,
    InstAccessFault = 1,
    IllegalInst = 2,
    Breakpoint = 3,
    LoadAddrMisaligned = 4,
    LoadAccessFault = 5,
    StoreAmoAddrMisaligned = 6,
    StoreAmoAccessFault = 7,
    UEcall = 8,
    SEcall = 9,
    InstPageFault = 12,
    LoadPageFault = 13,
    StoreAmoPageFault = 15,
    SoftwareCheck = 18,
    HardwareError = 19,
    // Interrupts
    SSoftwareIntr = MSB | 1,
    STimerIntr = MSB | 5,
    SExternalIntr = MSB | 9,
    CntOverflowIntr = MSB | 13,
};

struct scause : reg<scause, scause_enc> {
    using reg::reg; // ctor is protected

    static auto read() {
        sxlen_t tmp;
        asm volatile("csrr %0, scause"
                     : "=r"(tmp));
        return scause{ tmp };
    }
};

struct sepc : reg<sepc, sxlen_t> {
    using reg::reg;

    inline static auto read() {
        sxlen_t tmp;
        asm volatile("csrr %0, sepc"
                     : "=r"(tmp));
        return sepc{ tmp };
    }

    inline void write() const { asm volatile("csrw sepc, %0" ::"r"(value)); }

    inline void operator+=(sxlen_t inc) { value += inc; }
};

struct stval : reg<stval, sxlen_t> {
    using reg::reg;

    inline static auto read() {
        sxlen_t tmp;
        asm volatile("csrr %0, stval"
                     : "=r"(tmp));
        return stval{ tmp };
    }
};

class stvec {
    V value;

public:
    enum class Mode : V {
        Direct = 0,
        Vectored = 1,
    };

    explicit constexpr stvec(void (*handler)(), Mode mode = Mode::Direct)
        : value{ (reinterpret_cast<V>(handler) & ~V{ 0x3 }) | static_cast<V>(mode) } {}

    inline void write() { asm volatile("csrw stvec, %0" ::"r"(value)); }
};

enum class sstatus_enc : sxlen_t {
    SIE = 1 << 1,  // Supervisor Interrupt Enable
    SPIE = 1 << 5, // Supervisor Previous Interrupt Enable
    SUM = 1 << 18, // Permit Supervisor User Memory access
};

// For proc.cc user_entry
static constexpr sxlen_t USER_SSTATUS =
    static_cast<sxlen_t>(sstatus_enc::SUM) | static_cast<sxlen_t>(sstatus_enc::SPIE);

struct sstatus : reg<sstatus, sstatus_enc> {
    using reg::reg;

    static auto read() {
        sxlen_t tmp;
        asm volatile("csrr %0, sstatus"
                     : "=r"(tmp));
        return sstatus{ tmp };
    }

    void write() const { asm volatile("csrw sstatus, %0" ::"r"(value)); }

    constexpr sxlen_t raw() const { return value; }
};

struct satp {

private:
    V value;

    static constexpr V Sv32 = 1u << 31;
    static constexpr V Sv39 = 8ull << 60;

    static constexpr V mode() {
        if constexpr (__riscv_xlen == 32)
            return Sv32;
        else
            return Sv39;
    }

public:
    // Construct from a page table pointer
    explicit constexpr satp(paddr_t *page_table)
        // TODO PPN class with PTE class?
        : value((reinterpret_cast<V>(page_table) / PAGE_SIZE) | mode()) {}

    // Write to the CSR
    inline void write() const {
        asm volatile("sfence.vma\n"
                     "csrw satp, %0\n"
                     "sfence.vma\n"
                     :
                     : "r"(value));
    }
};

} // namespace csr
