// clang-format off
#include "ff.h"
#include "diskio.h"
// clang-format on
#include <ipc/blk.h>
#include <stddef.h>
#include <string.h>
#include <sys/ipc.h>

DSTATUS disk_status(BYTE pdrv) {
    if (0 != pdrv)
        return STA_NOINIT;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (0 != pdrv)
        return STA_NOINIT;
    // The user-space virtio-blk driver is brought up before f_mount is invoked.
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (0 != pdrv)
        return RES_PARERR;

    blk_req_t req = { 0 };
    blk_resp_t resp;
    req.op = BLK_OP_READ;
    /* Variable-size IPC: send only the header (24 B); receive header + count * 512.
     * Kernel cross_proc_memcpy uses min(sender_size, receiver_size). */
    const size_t req_hdr = offsetof(blk_req_t, data);
    const size_t resp_hdr = offsetof(blk_resp_t, data);
    while (count > 0) {
        UINT batch = count < BLK_MAX_SECTORS ? count : BLK_MAX_SECTORS;
        req.sector = sector;
        req.count = batch;
        if (ssend(&req, req_hdr, BLK_SERVER) < 0)
            return RES_ERROR;
        if (recv(&resp, resp_hdr + batch * BLK_SECTOR_BYTES, BLK_SERVER) < 0)
            return RES_ERROR;
        if (0 != resp.result)
            return RES_ERROR;
        memcpy(buff, resp.data, batch * BLK_SECTOR_BYTES);
        buff += batch * BLK_SECTOR_BYTES;
        sector += batch;
        count -= batch;
    }

    return RES_OK;
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (0 != pdrv)
        return RES_PARERR;

    blk_req_t req = { 0 };
    blk_resp_t resp;
    req.op = BLK_OP_WRITE;
    /* Send header + count * 512 of data; reply is just sizeof(int). */
    const size_t req_hdr = offsetof(blk_req_t, data);
    while (count > 0) {
        UINT batch = count < BLK_MAX_SECTORS ? count : BLK_MAX_SECTORS;
        req.sector = sector;
        req.count = batch;
        memcpy(req.data, buff, batch * BLK_SECTOR_BYTES);
        if (ssend(&req, req_hdr + batch * BLK_SECTOR_BYTES, BLK_SERVER) < 0)
            return RES_ERROR;
        if (recv(&resp, sizeof(int), BLK_SERVER) < 0)
            return RES_ERROR;
        if (0 != resp.result)
            return RES_ERROR;
        buff += batch * BLK_SECTOR_BYTES;
        sector += batch;
        count -= batch;
    }

    return RES_OK;
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (0 != pdrv)
        return RES_PARERR;

    blk_req_t req = { 0 };
    blk_resp_t resp;

    switch (cmd) {
    case CTRL_SYNC:
        req.op = BLK_OP_FLUSH;
        if (ssend(&req, offsetof(blk_req_t, data), BLK_SERVER) < 0)
            return RES_ERROR;
        if (recv(&resp, sizeof(int), BLK_SERVER) < 0)
            return RES_ERROR;
        return (0 == resp.result) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
        req.op = BLK_OP_IOCTL_SECTOR_COUNT;
        /* IOCTL: send header only, reply is just sizeof(int). */
        if (ssend(&req, offsetof(blk_req_t, data), BLK_SERVER) < 0)
            return RES_ERROR;
        if (recv(&resp, sizeof(int), BLK_SERVER) < 0)
            return RES_ERROR;
        *(LBA_t *) buff = (LBA_t) resp.result;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *) buff = BLK_SECTOR_BYTES;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *) buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
