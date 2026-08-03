// clang-format off
#include "ff.h"
#include "diskio.h"
// clang-format on
#include <ipc/blk.h>
#include <ipc/fs.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/misc.h>

static_assert(FF_USE_LFN == 0, "LFN needs ffunicode.c");
static_assert(FF_FS_REENTRANT == 0, "reentrancy needs ffsystem.c");

/////////////////
// diskio.c
/////////////////

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

    blk_req_t req = {};
    blk_resp_t resp;
    req.op = BLK_OP_READ;
    // Variable-size IPC: send only the header (20 B); receive header + count * 512.
    // Kernel cross_proc_memcpy uses min(sender_size, receiver_size).
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

    blk_req_t req = {};
    blk_resp_t resp;
    req.op = BLK_OP_WRITE;
    // Send header + count * 512 of data; reply is just sizeof(int).
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

    blk_req_t req = {};
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
        // IOCTL: send header only, reply is just sizeof(int).
        if (ssend(&req, offsetof(blk_req_t, data), BLK_SERVER) < 0)
            return RES_ERROR;
        if (recv(&resp, sizeof(int), BLK_SERVER) < 0)
            return RES_ERROR;
        *reinterpret_cast<LBA_t *>(buff) = static_cast<LBA_t>(resp.result);
        return RES_OK;

    case GET_SECTOR_SIZE:
        *reinterpret_cast<WORD *>(buff) = BLK_SECTOR_BYTES;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *reinterpret_cast<DWORD *>(buff) = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/////////////////////////
// Interface for fs_req_t
/////////////////////////

static FATFS g_fatfs;
static constexpr int MAX_FILES = 16;
static FIL fil_pool[MAX_FILES];
static bool fil_in_use[MAX_FILES];

static int alloc_fd() {
    for (int i = 0; i < MAX_FILES; ++i)
        if (!fil_in_use[i]) {
            fil_in_use[i] = true;
            return i + 1; // 1-based
        }
    return 0;
}

static void free_fd(int fd) {
    if (1 <= fd and fd <= MAX_FILES)
        fil_in_use[fd - 1] = false;
}

static FIL *get_fil(int fd) {
    if (fd < 1 or MAX_FILES < fd or !fil_in_use[fd - 1])
        return nullptr;
    return &fil_pool[fd - 1];
}

static BYTE mode_to_flags(const char *mode) {
    if (!mode)
        return 0;
    bool read = false, write = false, append = false, create = false;
    for (const char *p = mode; '\0' != *p; ++p)
        switch (*p) {
        case 'r':
            read = true;
            break;
        case 'w':
            write = true;
            create = true;
            break;
        case 'a':
            write = true;
            append = true;
            create = true;
            break;
        case '+':
            read = true;
            write = true;
            break;
        }
    BYTE flags = 0;
    if (read and !write)
        flags = FA_READ;
    else if (!read and write)
        flags = FA_WRITE;
    else if (read and write)
        flags = FA_READ | FA_WRITE;
    if (create)
        flags |= FA_CREATE_ALWAYS;
    if (append)
        flags |= FA_OPEN_APPEND;
    if (read and !create and !append)
        flags = FA_READ | FA_OPEN_EXISTING;
    return flags;
}

static fs_req_t req;

extern "C" void main() {
    FRESULT res = f_mount(&g_fatfs, "", 1);
    printf("fs: f_mount result %d\n", static_cast<int>(res));

    for (;;) {
        int_pid client = recv(&req, sizeof req, ANY_SOURCE);

        switch (req.op) {
        case FS_OP_OPEN: {
            fs_resp_ok_t result = -1;
            int fd = alloc_fd();
            if (fd) {
                BYTE flags = mode_to_flags(req.open.mode);
                if (flags) {
                    if (FR_OK == f_open(get_fil(fd), req.open.path, flags))
                        result = fd;
                    else
                        free_fd(fd);
                } else
                    free_fd(fd);
            }
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_CLOSE: {
            FIL *fil = get_fil(req.fd.fd);
            fs_resp_ok_t result = fil ? (f_close(fil), free_fd(req.fd.fd), 0) : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_READ: {
            fs_resp_read_t resp{};
            FIL *fil = get_fil(req.read.fd);
            if (!fil) {
                resp.result = -1;
            } else {
                UINT br = 0;
                unsigned to_read = req.read.size < FS_MAX_DATA ? req.read.size : FS_MAX_DATA;
                resp.result = (FR_OK == f_read(fil, resp.data, to_read, &br)) ? 0 : -1;
                resp.size = br;
            }
            ssend(&resp, sizeof resp, client);
            break;
        }
        case FS_OP_WRITE: {
            FIL *fil = get_fil(req.write.fd);
            fs_resp_ok_t result = -1;
            if (fil) {
                UINT bw = 0;
                unsigned to_write = req.write.size < FS_MAX_DATA ? req.write.size : FS_MAX_DATA;
                result = (FR_OK == f_write(fil, req.write.data, to_write, &bw)) ? static_cast<int>(bw) : -1;
            }
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_SEEK: {
            FIL *fil = get_fil(req.seek.fd);
            fs_resp_ok_t result = -1;
            if (fil) {
                FSIZE_t new_pos = 0;
                bool valid = true;
                if (SEEK_SET == req.seek.whence)
                    new_pos = req.seek.offset;
                else if (SEEK_CUR == req.seek.whence)
                    new_pos = f_tell(fil) + req.seek.offset;
                else if (SEEK_END == req.seek.whence)
                    new_pos = f_size(fil) + req.seek.offset;
                else
                    valid = false;
                if (valid)
                    result = (FR_OK == f_lseek(fil, new_pos)) ? 0 : -1;
            }
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_TELL: {
            FIL *fil = get_fil(req.fd.fd);
            fs_resp_ok_t result = fil ? static_cast<int>(f_tell(fil)) : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_FLUSH: {
            FIL *fil = get_fil(req.fd.fd);
            fs_resp_ok_t result = fil ? ((FR_OK == f_sync(fil)) ? 0 : -1) : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_EOF: {
            FIL *fil = get_fil(req.fd.fd);
            fs_resp_ok_t result = fil ? (f_eof(fil) ? 1 : 0) : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_REMOVE: {
            fs_resp_ok_t result = (FR_OK == f_unlink(req.path.path)) ? 0 : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_RENAME: {
            fs_resp_ok_t result = (FR_OK == f_rename(req.rename.path, req.rename.path2)) ? 0 : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        case FS_OP_MKDIR: {
            fs_resp_ok_t result = (FR_OK == f_mkdir(req.path.path)) ? 0 : -1;
            ssend(&result, sizeof result, client);
            break;
        }
        default: {
            fs_resp_ok_t result = -1;
            ssend(&result, sizeof result, client);
            break;
        }
        }
    }
}
