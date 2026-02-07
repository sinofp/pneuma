#include <sys/misc.h>
#include <sys/syscall.h>

int sleep_ms(unsigned ms) { return syscall(SYS_SLEEP, ms, 0, 0); }

// TODO span
int readfile(unsigned long fd, char *buf, size_t len) {
    return syscall(SYS_READFILE, fd, reinterpret_cast<unsigned long>(buf), len);
}

int writefile(unsigned long fd, const char *buf, size_t len) {
    return syscall(SYS_WRITEFILE, fd, reinterpret_cast<unsigned long>(buf), len);
}
