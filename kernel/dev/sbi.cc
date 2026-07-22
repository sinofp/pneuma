#include "dev/sbi.h"
#include "lib/hart.h"

namespace {

struct sbiret {
    long error;

    union {
        unsigned long uvalue;
        long value;
    };
};

constexpr decltype(sbiret::error) SBI_SUCCESS = 0;

enum class EID : xlen_t {
    DBCN = 0x4442434E,
    TIME = 0x54494D45,
    SRST = 0x53525354,
    HSM = 0x48534D,
    sPI = 0x735049, // sPI: s-mode IPI
};

// Many function arguments in SBI spec are defined as unsigned long.
// I prefer xlen_t for clarity, although they're the same.
sbiret sbi_call(xlen_t arg0, xlen_t arg1, xlen_t arg2, xlen_t arg3, xlen_t arg4, xlen_t arg5, xlen_t fid, EID eid) {
    register xlen_t a0 asm("a0") = arg0;
    register xlen_t a1 asm("a1") = arg1;
    register xlen_t a2 asm("a2") = arg2;
    register xlen_t a3 asm("a3") = arg3;
    register xlen_t a4 asm("a4") = arg4;
    register xlen_t a5 asm("a5") = arg5;
    register xlen_t a6 asm("a6") = fid;
    register auto a7 asm("a7") = static_cast<xlen_t>(eid);

    asm volatile("ecall"
                 : "=r"(a0), "=r"(a1)
                 : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    return { static_cast<long>(a0), { a1 } };
}

} // namespace

// DBCN: Console Write Byte (FID #2), struct sbiret sbi_debug_console_write_byte(uint8_t byte)
int putchar(int ch) {
    const auto ret = sbi_call(static_cast<uint8_t>(ch), 0, 0, 0, 0, 0, 2, EID::DBCN);
    if (SBI_SUCCESS == ret.error)
        return ch;
    else
        return EOF;
}

// DBCN: Console Read (FID #1), struct sbiret sbi_debug_console_read(unsigned long num_bytes, unsigned long
int getchar() {
    // base_addr_lo, unsigned long base_addr_hi)
    char ch = '\0'; // C Manual: 'Character values are returned as an unsigned char converted to an int'
    const unsigned long num_bytes = sizeof(char);
    const auto base_addr = reinterpret_cast<xlen_t>(&ch);
    const auto ret = sbi_call(num_bytes, base_addr, 0, 0, 0, 0, 1, EID::DBCN);
    if (SBI_SUCCESS == ret.error and num_bytes == ret.uvalue)
        return ch;
    else
        // This is a non-blocking call, so can return SBI_SUCCESS with no value. It's an error for us
        return EOF;
}

namespace sbi {

// Set Timer (FID #0) struct sbiret sbi_set_timer(uint64_t stime_value)
void set_timer(uint64_t stime_value) {
    // 'This function always returns SBI_SUCCESS in sbiret.error.'
    if (64 == __riscv_xlen) {
        sbi_call(stime_value, 0, 0, 0, 0, 0, 0, EID::TIME);
    } else {
        auto hi = static_cast<xlen_t>(stime_value >> 32);
        auto lo = static_cast<xlen_t>(stime_value);
        sbi_call(lo, hi, 0, 0, 0, 0, 0, EID::TIME);
    }
}

// System reset (FID #0) struct sbiret sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)
[[noreturn]]
void system_reset(uint32_t reset_type) {
    sbi_call(reset_type, 0, 0, 0, 0, 0, 0, EID::SRST);
    // ecall can return before system really resets
    for (;;)
        hart::wfi();
}

// Hart start (FID #0) struct sbiret sbi_hart_start(unsigned long hartid, unsigned long start_addr, unsigned long
// opaque)
long hart_start(xlen_t hartid, xlen_t start_addr, xlen_t opaque) {
    return sbi_call(hartid, start_addr, opaque, 0, 0, 0, 0, EID::HSM).error;
}

// Send IPI (FID #0) struct sbiret sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base)
void send_ipi(xlen_t hart_mask, xlen_t hart_mask_base) { sbi_call(hart_mask, hart_mask_base, 0, 0, 0, 0, 0, EID::sPI); }

} // namespace sbi
