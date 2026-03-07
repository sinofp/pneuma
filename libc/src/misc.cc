#include <stdlib.h>
#include <sys/misc.h>
#include <sys/syscall.h>

extern "C" {

int sleep_ms(unsigned ms) { return syscall(SYS_SLEEP, ms, 0, 0); }

} // extern "C"
