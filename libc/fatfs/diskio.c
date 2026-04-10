// clang-format off
#include "ff.h"
#include "diskio.h"
// clang-format on
#include <sys/syscall.h>

DSTATUS disk_status(BYTE pdrv) {
    if (0 != pdrv)
        return STA_NOINIT; // Only drive 0 supported

    return 0; // Drive is always ready
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (0 != pdrv)
        return STA_NOINIT; // Only drive 0 supported

    // VirtIO block device is already initialized by kernel
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (0 != pdrv)
        return RES_PARERR;

    for (UINT i = 0; i < count; i++) {
        long ret = syscall(SYS_DISK_READ, sector + i, (unsigned long) (buff + i * 512), 0);
        if (0 != ret)
            return RES_ERROR;
    }

    return RES_OK;
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (0 != pdrv)
        return RES_PARERR;

    for (UINT i = 0; i < count; i++) {
        long ret = syscall(SYS_DISK_WRITE, sector + i, (unsigned long) (buff + i * 512), 0);
        if (0 != ret)
            return RES_ERROR;
    }

    return RES_OK;
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (0 != pdrv)
        return RES_PARERR;

    switch (cmd) {
    case CTRL_SYNC:
        // No buffering, sync is a no-op
        return RES_OK;

    case GET_SECTOR_COUNT: {
        LBA_t count = (LBA_t) syscall(SYS_DISK_IOCTL, DISK_IOCTL_GET_SECTOR_COUNT, 0, 0);
        *(LBA_t *) buff = count;
        return RES_OK;
    }

    case GET_SECTOR_SIZE:
        *(WORD *) buff = 512;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *) buff = 1; // Erase block size in sectors
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
