#include <sys/misc.h>
#include <sys/syscall.h>

int sleep_ms(unsigned ms) { return syscall(SYS_SLEEP, ms, 0, 0); }
