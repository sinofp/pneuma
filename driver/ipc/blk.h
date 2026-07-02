#pragma once

#include <stdint.h>

// clang-format off
#define BLK_SERVER                 2 /* PID; matches spawn order in kernel/main.cc */
#define BLK_SECTOR_BYTES           512
#define BLK_MAX_SECTORS            4 /* max FatFs count observed across full Doom boot; payload = 2 KB */
#define BLK_BATCH_BYTES            (BLK_MAX_SECTORS * BLK_SECTOR_BYTES)

#define BLK_OP_READ                1
#define BLK_OP_WRITE               2
#define BLK_OP_IOCTL_SECTOR_COUNT  3
#define BLK_OP_IOCTL_SECTOR_SIZE   4
#define BLK_OP_IOCTL_BLOCK_SIZE    5
#define BLK_OP_FLUSH               6 /* VIRTIO_BLK_T_FLUSH: persist write cache to medium */

typedef struct {
    int op;
    uint64_t sector;                 /* first sector (READ/WRITE) */
    uint32_t count;                  /* 1..BLK_MAX_SECTORS for READ/WRITE; unused for IOCTL */
    char data[BLK_BATCH_BYTES];      /* payload on WRITE */
} blk_req_t;

typedef struct {
    int result;                      /* 0 OK / < 0 error; for IOCTL: the queried value */
    char data[BLK_BATCH_BYTES];      /* payload on READ */
} blk_resp_t;

// clang-format on
