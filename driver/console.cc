#include "stddef.h"
#include "virtio_helpers.h"
#include <ipc/console.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/misc.h>

// There's only UART backend.
// However, I name this file console.cc in case in the future I have a framebuffer based console.

// As a last resort to report error
void putstring(const char *s) {
    while (*s)
        syscall(SYS_PUTCHAR, *s++, 0, 0);
}

// This server must never exit, as libc relies on it.
[[noreturn]]
void park(const char *why) {
    putstring("console: ");
    putstring(why);
    putstring("\n");
    for (;;)
        sleep_ms(1000);
}

// The assupmption is that kernel's SBI calls only touch these registers on PANIC/fatal_trap
// Therefore it's safe to share the device on normal occasion.
enum class reg_off : size_t {
    RBR_THR = 0, // Receive Buffer Register (R), Transmit Holding Register (W)
    IIR_FCR = 2, // Interrupt Identifier Register (R), FIFO Control Register (W)
    LSR = 5,     // Line Status Register
    // SNPS, no reg-shift for below:
    // USR = 0x7C, // UART Status Register
    // CPR = 0xF4, // Component Parameter Register
    CTR = 0xFC, // Component Type Register
};

constexpr uint32_t LSR_THRE = 1 << 5; // Transmit Holding Register Empty
constexpr uint32_t LSR_OE = 1 << 1;   // Overrun Error
constexpr uint32_t LSR_DR = 1;        // Data Ready

constexpr size_t FIFO_depth = 16;
static_assert(1 < FIFO_depth, "No space to insert CR before LF");

class Reg {
    unsigned long addr;
    uint32_t reg_io_width;

public:
    Reg(unsigned long base, reg_off offset, uint32_t reg_shift, uint32_t io_width)
        : addr{ base + (static_cast<size_t>(offset) << reg_shift) }
        , reg_io_width{ io_width } {};
    Reg(const Reg &) = delete;
    Reg &operator=(const Reg &) = delete;

    void operator=(uint32_t data) {
        switch (reg_io_width) {
        case 1:
            virtio::mmio_write(addr, 0, static_cast<uint8_t>(data));
            break;
        case 4:
            virtio::mmio_write(addr, 0, data);
            break;
        default:
            park("reg_io_width is invalid\n");
        }
    }

    operator uint32_t() {
        switch (reg_io_width) {
        case 1:
            return virtio::mmio_read<uint8_t>(addr, 0);
        case 4:
            return virtio::mmio_read<uint32_t>(addr, 0);
        default:
            park("reg_io_width is invalid\n");
        }
    }
};

extern "C" int main() {
    fdt_dev_t devs[1];
    auto n = fdt_query("snps,dw-apb-uart", devs, sizeof(devs) / sizeof(devs[0]));
    bool isSNPS = n;
    if (!isSNPS)
        n = fdt_query("ns16550a", devs, sizeof(devs) / sizeof(devs[0]));

    if (1 != n) {
        park("No UART device discovered");
    }
    const auto &dev = devs[0];

    if (!mmio_map(dev.mmio_base, (4095 + dev.mmio_size) / 4096)) {
        park("Can't map UART memory");
    }

    Reg LSR{ dev.mmio_base, reg_off::LSR, dev.reg_shift, dev.reg_io_width };
    Reg RBR_THR{ dev.mmio_base, reg_off::RBR_THR, dev.reg_shift, dev.reg_io_width };
    Reg IIR_FCR{ dev.mmio_base, reg_off::IIR_FCR, dev.reg_shift, dev.reg_io_width };

    IIR_FCR = 1; // FIFOE
    bool FIFO_enabled = ((IIR_FCR >> 6) & 0b11) == 0b11;

    if (!FIFO_enabled)
        park("FIFO not enabled");

    if (isSNPS) {
        Reg CTR{ dev.mmio_base, reg_off::CTR, 0, dev.reg_io_width };

        if (0x44570110 != CTR)
            park("Not genuine snps,dw-apb-uart");
    }

    console_req_t req;
    console_resp_t resp;

    for (;;) {
        int_pid client = recv(&req, sizeof req, ANY_SOURCE);
        if (client < 0)
            continue;

        switch (req.op) {
        case CONSOLE_OP_WRITE:
            if (CONSOLE_MAX_DATA < req.len)
                req.len = CONSOLE_MAX_DATA;

            {
                bool needs_cr = true;
                for (size_t i = 0, j, inserted; i < req.len; i += j) {
                    while (!(LSR & LSR_THRE))
                        ;
                    for (j = 0, inserted = 0; j < (FIFO_depth - inserted) and (i + j) < req.len;)
                        if (auto ch = req.data[i + j]; '\n' == ch and needs_cr) {
                            RBR_THR = '\r';
                            needs_cr = false;
                            ++inserted;
                        } else {
                            RBR_THR = ch;
                            needs_cr = true;
                            ++j;
                        }
                }
            }

            break;
        case CONSOLE_OP_READ:
            resp = {};

            {
                uint32_t lsr = LSR;
                if (lsr & LSR_OE)
                    putstring("New char received before the previous data was read");
                if (lsr & LSR_DR) {
                    resp.has_char = true;
                    resp.ch = RBR_THR;
                }
                // Return an empty resp with has_char = false, so getchar can't block putchar
            }
            ssend(&resp, sizeof(resp), client);
            break;
        default:
            putstring("Unkown op");
        }
    }
}
