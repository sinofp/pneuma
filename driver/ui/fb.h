#pragma once

#include <ipc/gpu.h>
#include <sys/ipc.h>
#include <syscalls.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int fb_open(void *buffer, int width, int height) {
    const unsigned long fb_vaddr = (unsigned long) buffer;
    const size_t fb_len = width * height * sizeof(uint32_t);

    gpu_req_t req = { 0 };
    req.op = GPU_OP_ATTACH;
    req.width = width;
    req.height = height;
    req.head_off = fb_vaddr & 0xFFF; // PAGE_SIZE - 1
    req.nr_pages = page_walk(fb_vaddr, fb_len, req.paddrs);

    const size_t send_bytes = offsetof(gpu_req_t, paddrs) + req.nr_pages * sizeof(uint64_t);
    ssend(&req, send_bytes, GPU_SERVER);
    gpu_resp_t resp;
    recv(&resp, sizeof resp, GPU_SERVER);

    return resp.result;
}

static inline int fb_update() {
    gpu_req_t req = { 0 };
    req.op = GPU_OP_FLUSH;
    ssend(&req, offsetof(gpu_req_t, paddrs), GPU_SERVER);
    gpu_resp_t resp;
    recv(&resp, sizeof resp, GPU_SERVER);

    return resp.result;
}

static inline void fb_close() {}

#ifdef __cplusplus
}
#endif
