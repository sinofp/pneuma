#include "dev/sbi.h"
#include "lib/common.h"
#include "lib/hart.h"
#include "proc/proc.h"

namespace {

constexpr size_t NO_PANICKER = static_cast<size_t>(-1);
// Hart id of the first panicking hart; it owns the console until shutdown.
size_t panicker = NO_PANICKER;

[[noreturn]]
void park() {
    for (;;)
        hart::wfi();
}

} // namespace

namespace panic {

void park_if_panicking() {
    const auto holder = __atomic_load_n(&panicker, __ATOMIC_ACQUIRE);
    if (NO_PANICKER == holder or hart::id() == holder)
        return;
    park();
}

void prologue() {
    auto expected = NO_PANICKER;
    if (!__atomic_compare_exchange_n(&panicker, &expected, hart::id(), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (hart::id() == expected) {
            // Recursive panic: printf/backtrace themselves are faulting.
            for (const char *p = "\nRECURSIVE PANIC\n"; *p; ++p)
                putchar(*p);
        }
        // else: another hart panicked first and owns the console
        park();
    }

    // Before IPI because IPI might also be fatal
    auto *curr = proc::currents[hart::id()];
    printf_unlocked("\nPANIC on hart %d, pid %d\n", static_cast<int>(hart::id()),
                    curr ? static_cast<int>(curr->pid) : -1);

    // IPI other started harts (panic might precede kernel_secondary_mains) -> handle_trap -> call park_if_panicking
    xlen_t mask = 0;
    for (auto h = 0uz; h < hart::MAX; ++h)
        mask |= static_cast<xlen_t>(hart::id() != h and hart::started[h]) << h;
    sbi::send_ipi(mask, 0);
}

} // namespace panic
