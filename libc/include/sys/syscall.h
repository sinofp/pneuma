#pragma once

// Syscall numbers
// Technically, they should be xlen_t, because they need to fit in a0
// However, we don't want to expose xlen_t to user applications.
constexpr unsigned long SYS_PUTCHAR = 1;
constexpr unsigned long SYS_GETCHAR = 2;
constexpr unsigned long SYS_EXIT = 3;
constexpr unsigned long SYS_DISK_READ = 4;
constexpr unsigned long SYS_DISK_WRITE = 5;
constexpr unsigned long SYS_SLEEP = 6;
constexpr unsigned long SYS_DISK_IOCTL = 7;

// DISK_IOCTL commands
constexpr unsigned long DISK_IOCTL_GET_SECTOR_COUNT = 1;
constexpr unsigned long DISK_IOCTL_GET_SECTOR_SIZE = 2;
constexpr unsigned long DISK_IOCTL_GET_BLOCK_SIZE = 3;

// Raw syscall interface
#ifdef __cplusplus
extern "C" {
#endif

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2);

#ifdef __cplusplus
}
#endif
