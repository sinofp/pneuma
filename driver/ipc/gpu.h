#pragma once

#include <stdint.h>

#define GPU_SERVER    4 // PID; matches spawn order in kernel/main.cc

// 512 KB fb ceiling. Doom's 320x200x4 = 250 KB spans up to 63 whole pages plus a possible
// extra page if the buffer isn't page-aligned, so 128 leaves ample headroom.
#define GPU_MAX_PAGES 128

#define GPU_OP_ATTACH 1
#define GPU_OP_FLUSH  2

typedef struct {
    int op;
    uint32_t width, height;
    uint32_t nr_pages;              // meaningful only for ATTACH
    uint32_t head_off;              // offset of the first byte within paddrs[0]; 0 if fb is page-aligned
    uint64_t paddrs[GPU_MAX_PAGES]; // per-page paddrs backing the framebuffer
} gpu_req_t;

typedef struct {
    int result; // 0 OK / < 0 error
} gpu_resp_t;
