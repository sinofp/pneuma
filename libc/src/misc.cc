#include <stdlib.h>
#include <sys/misc.h>
#include <sys/stat.h>
#include <sys/syscall.h>

extern "C" {

int sleep_ms(unsigned ms) { return syscall(SYS_SLEEP, ms, 0, 0); }

// Stubs for doomgeneric
int mkdir(const char *path, mode_t mode) {
    (void) path;
    (void) mode;
    return 0;
}

int stat(const char *path, struct stat *buf) {
    (void) path;
    (void) buf;
    return -1;
}

int system(const char *command) {
    (void) command;
    return -1;
}

} // extern "C"
