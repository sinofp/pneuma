#include "fdt/fdt.h"
#include "dev/mmio.h"
#include "lib/common.h"
#include "lib/hart.h"
#include "lib/panic.h"
#include "lib/span.h"
#include "lib/symbols.h"
#include "mem/mem.h"
#include "trap/plic.h"
#include <virtio.h>

using fdt::uninitialised;

namespace {
constexpr size_t MAX_VIRTIO_DEVS = 8;
constexpr size_t MAX_UART_DEVS = 1;
size_t virtio_dev_cnt = 0, uart_dev_cnt = 0;

constexpr uint32_t GPU_DEVICE_ID = 16;

static_assert(fdt::MAX_DEVS == MAX_VIRTIO_DEVS + MAX_UART_DEVS, "devs[] must hold every kind at once");

enum class FDT : uint32_t {
    MAGIC = 0xd00dfeed,
    BEGIN_NODE = 1,
    END_NODE,
    PROP,
    NOP,
    END = 9,
};

template <typename T> struct be;

template <> struct be<uint32_t> {
    uint32_t v;

    inline constexpr operator uint32_t() const { return __builtin_bswap32(v); }
};

template <> struct be<uint64_t> {
    uint64_t v;

    inline constexpr operator uint64_t() const { return __builtin_bswap64(v); }
};

inline uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }

class cell_view {
    span<const be<uint32_t>> cells;
    size_t idx = 0;

public:
    constexpr cell_view()
        : cells(nullptr, 0) {}

    explicit constexpr cell_view(span<const be<uint32_t>> view)
        : cells(view) {}

    bool empty() const { return idx == cells.size(); }

    uint32_t take_u32() {
        if (empty())
            return uninitialised;
        uint32_t v = cells.data()[idx];
        ++idx;
        return v;
    }

    uint64_t take_u64(int count) {
        uint64_t val = 0;
        for (int i = 0; i < count; ++i)
            // FDT cells are big endian; concatenate high-to-low.
            val = (val << 32) | take_u32();
        return val;
    }
};

bool startswith(const char *string, const char *value) {
    auto i = strlen(string);
    auto len = strlen(value);
    if (i < len)
        return false;
    for (i = 0; i != len; ++i)
        if (string[i] != value[i])
            return false;
    return true;
}

// A string-list property ("compatible", "status", ...) is NUL-terminated strings packed back-to-back,
// so the list only makes sense together with its length -- taking a span keeps the two inseparable.
bool string_prop_has(span<const char> list, const char *target) {
    const auto *data = list.data();
    const auto len = list.size();
    size_t i = 0;
    while (i < len) {
        const char *s = data + i;
        size_t j = 0;
        while (i + j < len and s[j] != '\0')
            ++j;
        if (len <= i + j)
            break;
        if (!strcmp(s, target))
            return true;

        i += j + 1;
    }
    return false;
}

struct node_ctx {
    int address_cells = -1;
    int size_cells = -1;
    // Points into the DTB blob, which mem::reserve keeps alive for the life of the kernel.
    span<const char> compatible;
    bool is_memory;
    bool is_rsvd_memory;
    bool is_cpus;
    bool is_cpu;
    bool is_interrupt_controller;
    bool is_virtio;
    bool is_uart;
    bool is_plic;
    uint64_t reg_addr = uninitialised;
    uint64_t reg_size = uninitialised;
    uint32_t reg_shift = 0;
    uint32_t reg_io_width = 1;
    uint32_t irq = uninitialised; // TODO move out?
    cell_view irq_ext;
    bool reg_set;              // TODO remove?
    bool reg_has_more = false; // reg holds further (address, size) pairs beyond the one parsed
    bool irq_set;              // matches interrupts or interrupts-extended (only for PLIC) TODO remove?
    uint32_t phandle = uninitialised;
    bool has_mmu = false;  // TODO remove?
    bool status_ok = true; // absent is okay
    bool is_s7 = false;
};

constexpr size_t max_depth = 16;
} // namespace

namespace fdt {
fdt_dev_entry devs[MAX_DEVS];
size_t dev_cnt = 0;
} // namespace fdt

namespace plic {
paddr_t start_addr = uninitialised; // 0x0c000000;
paddr_t end_addr = uninitialised;   // 0x00600000;
uint32_t irq_ext[irq_ext_n];
uint32_t hart_to_phandle[hart::MAX];
} // namespace plic

paddr_t free_ram_end;

namespace fdt {

uint32_t clk_freq_ms = uninitialised;

bool is_compatible(const fdt_dev_entry &entry, const char *target) { return string_prop_has(entry.compatible, target); }

bool has_gpu = false;

// https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html#header
struct header {
    be<uint32_t> magic;
    be<uint32_t> totalsize;
    be<uint32_t> off_dt_struct;
    be<uint32_t> off_dt_strings;
    be<uint32_t> off_mem_rsvmap;
    be<uint32_t> version;
    be<uint32_t> last_comp_version;
    be<uint32_t> boot_cpuid_phys;
    be<uint32_t> size_dt_strings;
    be<uint32_t> size_dt_struct;
};

void init(const header *fdt_ptr) {
    // Header fields are BE; be<T> converts them on access.
    if (static_cast<uint32_t>(fdt_ptr->magic) != static_cast<uint32_t>(FDT::MAGIC))
        PANIC("Wrong FDT header magic");

    // So we can use pointer to compatible safely
    mem::reserve({ reinterpret_cast<const uint8_t *>(fdt_ptr), static_cast<uint32_t>(fdt_ptr->totalsize) });

    auto *struct_base = reinterpret_cast<const uint8_t *>(fdt_ptr) + static_cast<uint32_t>(fdt_ptr->off_dt_struct);
    auto *strings_base = reinterpret_cast<const char *>(fdt_ptr) + static_cast<uint32_t>(fdt_ptr->off_dt_strings);
    auto *struct_end = struct_base + static_cast<uint32_t>(fdt_ptr->size_dt_struct);

    const auto *p = reinterpret_cast<const be<uint32_t> *>(struct_base);
    const auto *end = reinterpret_cast<const be<uint32_t> *>(struct_end);

    node_ctx stack[max_depth];
    size_t depth = -1;

    while (p < end) {
        auto token = static_cast<FDT>(static_cast<uint32_t>(*p++));
        switch (token) {
        case FDT::NOP:
            continue;
        case FDT::END:
            return;
        case FDT::BEGIN_NODE: {
            const char *name = reinterpret_cast<const char *>(p);
            uint32_t name_len = 0;
            while (name_len < 1024 and name[name_len] != '\0')
                ++name_len;
            uint32_t skip = align4(name_len + 1);
            p = reinterpret_cast<const be<uint32_t> *>(reinterpret_cast<const uint8_t *>(p) + skip);

            if (++depth < max_depth)
                stack[depth] = { .is_memory = startswith(name, "memory@"),
                                 .is_rsvd_memory = !strcmp(name, "reserved-memory"),
                                 .is_cpus = !strcmp(name, "cpus"),
                                 .is_cpu = startswith(name, "cpu@"),
                                 .is_interrupt_controller = !strcmp(name, "interrupt-controller") };
            else
                PANIC("FDT exceeds maximum depth");
            continue;
        }
        case FDT::END_NODE: {
            if (depth < 0)
                PANIC("FDT_END_NODE before the first FDT_BEGIN_NODE");
            auto &ctx = stack[depth];
            if (ctx.is_virtio and ctx.reg_set and ctx.irq_set and virtio_dev_cnt < MAX_VIRTIO_DEVS) {
                if (auto device_id = mmio::read<uint32_t>(static_cast<paddr_t>(ctx.reg_addr), virtio::REG_DEVICE_ID);
                    device_id) {
                    ++virtio_dev_cnt;
                    has_gpu |= GPU_DEVICE_ID == device_id;
                    devs[dev_cnt++] = { .dev = { .mmio_base = static_cast<paddr_t>(ctx.reg_addr),
                                                 .mmio_size = static_cast<unsigned long>(ctx.reg_size),
                                                 .irq = ctx.irq,
                                                 .device_id = device_id },
                                        .compatible = ctx.compatible };
                }
            } else if (ctx.is_uart and ctx.reg_set and ctx.irq_set and ctx.status_ok and uart_dev_cnt < MAX_UART_DEVS) {
                ++uart_dev_cnt;
                // TODO paser chosen and select the initialised UART using /chosen/stdout-path
                devs[dev_cnt++] = { .dev = { .mmio_base = static_cast<paddr_t>(ctx.reg_addr),
                                             .mmio_size = static_cast<unsigned long>(ctx.reg_size),
                                             .reg_shift = ctx.reg_shift,
                                             .reg_io_width = ctx.reg_io_width,
                                             .irq = ctx.irq },
                                    .compatible = ctx.compatible };
            } else if (ctx.is_plic and ctx.reg_set and ctx.irq_set) {
                plic::start_addr = static_cast<paddr_t>(ctx.reg_addr);
                plic::end_addr = plic::start_addr + ctx.reg_size;
                for (size_t i = 0; !ctx.irq_ext.empty() and i < plic::irq_ext_n; ++i)
                    plic::irq_ext[i] = ctx.irq_ext.take_u32();
            } else if (ctx.is_interrupt_controller and uninitialised != ctx.phandle and stack[depth - 1].is_cpu)
                plic::hart_to_phandle[stack[depth - 1].reg_addr] = ctx.phandle;
            else if (ctx.is_cpu and ctx.reg_set and ctx.reg_addr < hart::MAX)
                // s7 in U-Boot FDT is marked with mmu-type = "riscv,sv39"; and status = "okay"
                hart::startable[ctx.reg_addr] = ctx.status_ok and ctx.has_mmu and !ctx.is_s7;
            else if (ctx.is_memory and ctx.reg_set) {
                free_ram_end = ctx.reg_addr + ctx.reg_size;
                // printf("fdt::init: memory addr: 0x%lx, size: 0x%lx, calculated free_ram_end: %p\n", ctx.reg_addr,
                //        ctx.reg_size, free_ram_end);
            } else if (0 < depth and stack[depth - 1].is_rsvd_memory and ctx.reg_set) {
                if (ctx.reg_has_more) [[unlikely]]
                    PANIC("reserved memory node has a multi-pair reg; only the first pair is parsed");

                if (ctx.reg_addr < reinterpret_cast<paddr_t>(__free_ram) and
                    reinterpret_cast<paddr_t>(__kernel_base) < ctx.reg_addr + ctx.reg_size) [[unlikely]]
                    PANIC("reserved memory overlaps the kernel image"); // [__kernel_base, __free_ram)

                if (reinterpret_cast<paddr_t>(__free_ram) <= ctx.reg_addr) [[unlikely]]
                    mem::reserve({ reinterpret_cast<const uint8_t *>(static_cast<paddr_t>(ctx.reg_addr)),
                                   static_cast<size_t>(ctx.reg_size) });
            }
            --depth;
            continue;
        }
        case FDT::PROP: {
            if (depth < 0)
                PANIC("FDT_PROP before the first FDT_BEGIN_NODE");

            auto len = static_cast<uint32_t>(*p++);
            auto nameoff = static_cast<uint32_t>(*p++);
            const char *prop_name = strings_base + nameoff;
            const auto *prop_data = reinterpret_cast<const uint8_t *>(p);
            p = reinterpret_cast<const be<uint32_t> *>(prop_data + align4(len));

            auto &ctx = stack[depth];
            // #address-cells/#size-cells describe how children encode their reg.
            if (!strcmp(prop_name, "#address-cells") and sizeof(be<uint32_t>) <= len) {
                ctx.address_cells = static_cast<int>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
            } else if (!strcmp(prop_name, "#size-cells") and sizeof(be<uint32_t>) <= len) {
                ctx.size_cells = static_cast<int>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
                continue;
            }

            // We only care #address-cells and #size-cells in the root node.
            // We guard the 0 == depth here so we can use stack[depth-1] afterwards.
            if (!depth)
                continue;

            if (ctx.is_cpus and !strcmp(prop_name, "timebase-frequency") and sizeof(be<uint32_t>) <= len) {
                // DT spec defines timebase-frequency as a single 32-bit cell.
                const auto hz = static_cast<uint32_t>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
                fdt::clk_freq_ms = hz / 1000u;
            } else if (!strcmp(prop_name, "compatible") and len) {
                ctx.compatible = { reinterpret_cast<const char *>(prop_data), len };
                if (string_prop_has(ctx.compatible, "virtio,mmio"))
                    ctx.is_virtio = true;
                else if (string_prop_has(ctx.compatible, "ns16550a") or
                         string_prop_has(ctx.compatible, "snps,dw-apb-uart"))
                    ctx.is_uart = true;
                else if (string_prop_has(ctx.compatible, "riscv,plic0") or
                         string_prop_has(ctx.compatible, "sifive,plic-1.0.0"))
                    ctx.is_plic = true;
                else if (string_prop_has(ctx.compatible, "sifive,s7"))
                    ctx.is_s7 = true;
                continue;
            }

            // reg is encoded using the parent address/size cells.
            auto &parent = stack[depth - 1];
            if (!strcmp(prop_name, "reg") and parent.address_cells != -1 and parent.size_cells != -1 and
                static_cast<uint32_t>((parent.address_cells + parent.size_cells) * sizeof(be<uint32_t>)) <= len) {
                auto cells = cell_view{ span<const be<uint32_t>>(
                    reinterpret_cast<const be<uint32_t> *>(prop_data),
                    static_cast<size_t>(parent.address_cells + parent.size_cells)) };
                // - reg = <0x00 0x10001000 0x00 0x1000> with parent #addres-cells == 2 and #size-cells == 2
                // - reg_addr = take_u64(2) → (0x00 << 32) | 0x10001000 → 0x0000000010001000
                // - reg_size = take_u64(2) → (0x00 << 32) | 0x1000 → 0x0000000000001000
                // For a cpu node, its parent.address_cells=1, parent.size_cells=0
                ctx.reg_addr = cells.take_u64(parent.address_cells);
                ctx.reg_size = cells.take_u64(parent.size_cells);
                ctx.reg_set = true;
                // A reg may carry several pairs while the cell_view above spans exactly one, so the rest go unread.
                // Record that so callers who cannot tolerate the loss can say so.
                const auto pair_bytes =
                    static_cast<uint32_t>((parent.address_cells + parent.size_cells) * sizeof(be<uint32_t>));
                ctx.reg_has_more = pair_bytes < len;
            } else if (!strcmp(prop_name, "reg-shift") and sizeof(be<uint32_t>) <= len)
                ctx.reg_shift = static_cast<uint32_t>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
            else if (!strcmp(prop_name, "reg-io-width") and sizeof(be<uint32_t>) <= len)
                ctx.reg_io_width = static_cast<uint32_t>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
            else if (!strcmp(prop_name, "interrupts") and sizeof(be<uint32_t>) <= len) {
                ctx.irq = static_cast<uint32_t>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
                ctx.irq_set = true;
            } else if (!strcmp(prop_name, "interrupts-extended") and
                       2 * sizeof(be<uint32_t>) <= len /* at least one pair */) {
                ctx.irq_ext = cell_view{ span<const be<uint32_t>>(reinterpret_cast<const be<uint32_t> *>(prop_data),
                                                                  len / sizeof(be<uint32_t>)) };
                ctx.irq_set = true;
            } else if (!strcmp(prop_name, "phandle") and sizeof(be<uint32_t>) <= len) {
                ctx.phandle = static_cast<uint32_t>(*reinterpret_cast<const be<uint32_t> *>(prop_data));
            } else if (!strcmp(prop_name, "mmu-type") and len)
                // VF2 is Sv39, virt 32 is Sv32, while virt 64 is Sv57
                ctx.has_mmu = startswith(reinterpret_cast<const char *>(prop_data), "riscv,sv");
            else if (!strcmp(prop_name, "status") and 4 <= len)
                ctx.status_ok = string_prop_has({ reinterpret_cast<const char *>(prop_data), len }, "okay");

        } break;
        default:
            PANIC("Unkown token in FDT");
        }
    }
}
} // namespace fdt
