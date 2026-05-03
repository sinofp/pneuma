#pragma once

// Can't use constexpr because C user programs can't use it

// Syscall numbers
#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT 3
#define SYS_DISK_READ 4
#define SYS_DISK_WRITE 5
#define SYS_SLEEP 6
#define SYS_DISK_IOCTL 7
#define SYS_GET_TICKS 8
#define SYS_GPU_ATTACH 9
#define SYS_GPU_FLUSH 10
#define SYS_GET_INPUT 11
#define SYS_MP_SSEND 12
#define SYS_MP_RECV 13

// DISK_IOCTL commands
#define DISK_IOCTL_GET_SECTOR_COUNT 1
#define DISK_IOCTL_GET_SECTOR_SIZE 2
#define DISK_IOCTL_GET_BLOCK_SIZE 3

// Raw syscall interface
#ifdef __cplusplus
extern "C" {
#endif

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2);

#ifdef __cplusplus
}
#endif
