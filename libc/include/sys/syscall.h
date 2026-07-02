#pragma once

// Can't use constexpr because C user programs can't use it

// Syscall numbers (4, 5, 7 vacated by SYS_DISK_* migration; do not renumber survivors)
#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT 3
#define SYS_SLEEP 6
#define SYS_GET_TICKS 8
#define SYS_GPU_ATTACH 9
#define SYS_GPU_FLUSH 10
#define SYS_GET_INPUT 11
#define SYS_MP_SSEND 12
#define SYS_MP_RECV 13
#define SYS_FDT_VIRTIO 14
#define SYS_MMIO_MAP 15
#define SYS_DMA_ALLOC 16
#define SYS_IRQ_WAIT 17

// Raw syscall interface
#ifdef __cplusplus
extern "C" {
#endif

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2);

#ifdef __cplusplus
}
#endif
