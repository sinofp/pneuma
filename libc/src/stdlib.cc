#include <stdlib.h>
#include <sys/syscall.h>

[[noreturn]]
void exit(int status /*status is ignored for now*/) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;)
        ; // Just in case!
}
