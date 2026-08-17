#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/misc.h>
#include <sys/syscall.h>

extern "C" {

int sleep_ms(unsigned ms) { return syscall(SYS_SLEEP, ms, 0, 0); }

unsigned get_ticks_ms() { return syscall(SYS_GET_TICKS, 0, 0, 0); }

void reboot() { syscall(SYS_REBOOT, 0, 0, 0); }

int_pid ssend(void *src, size_t n, int_pid recipient) {
    return syscall(SYS_MP_SSEND, (unsigned long) src, n, recipient);
}

int_pid recv(void *dst, size_t n, int_pid sender) { return syscall(SYS_MP_RECV, (unsigned long) dst, n, sender); }

} // extern "C"
