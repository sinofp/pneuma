#include "doomgeneric.h"
#include "doomkeys.h"
#include "m_fixed.h"
#include <ipc/gpu.h>
#include <ipc/input.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/misc.h>
#include <sys/syscall.h>
#include <syscalls.h>

// Use double to fake Doom 16.16 fixed-point to replace 64-bit int divison on RV32
fixed_t FixedMul(fixed_t a, fixed_t b) { return (fixed_t) ((double) a * b / FRACUNIT); }

fixed_t FixedDiv(fixed_t a, fixed_t b) {
    if ((abs(a) >> 14) >= abs(b))
        return (a ^ b) < 0 ? INT_MIN : INT_MAX;
    return (fixed_t) ((double) a / b * FRACUNIT);
}

void DG_Init(void) {
    // DG_ScreenBuffer is defined and allocated in doomgeneric.c
    const unsigned long fb_vaddr = (unsigned long) DG_ScreenBuffer;
    const size_t fb_len = DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(uint32_t);

    gpu_req_t req = { 0 };
    req.op = GPU_OP_ATTACH;
    req.width = DOOMGENERIC_RESX;
    req.height = DOOMGENERIC_RESY;
    req.head_off = fb_vaddr & 0xFFF; // PAGE_SIZE - 1
    req.nr_pages = page_walk(fb_vaddr, fb_len, req.paddrs);

    const size_t send_bytes = offsetof(gpu_req_t, paddrs) + req.nr_pages * sizeof(uint64_t);
    ssend(&req, send_bytes, GPU_SERVER);
    gpu_resp_t resp;
    recv(&resp, sizeof resp, GPU_SERVER);
}

void DG_DrawFrame(void) {
    gpu_req_t req = { 0 };
    req.op = GPU_OP_FLUSH;
    ssend(&req, offsetof(gpu_req_t, paddrs), GPU_SERVER);
    gpu_resp_t resp;
    recv(&resp, sizeof resp, GPU_SERVER);
}

void DG_SleepMs(uint32_t ms) { sleep_ms(ms); }

uint32_t DG_GetTicksMs(void) { return syscall(SYS_GET_TICKS, 0, 0, 0); }

// Linux/virtio key code ranges mapped to ASCII
static const unsigned char keyboard_nums[10] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };
static const unsigned char keyboard_qwerty[12] = { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']' };
static const unsigned char keyboard_asdf[11] = { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'' };
static const unsigned char keyboard_zxcv[11] = { '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/' };

static unsigned char toDoomKey(unsigned int code) {
    // Arrow keys
    if (code == 103)
        return KEY_UPARROW;
    if (code == 108)
        return KEY_DOWNARROW;
    if (code == 105)
        return KEY_LEFTARROW;
    if (code == 106)
        return KEY_RIGHTARROW;

    // F Keys
    if ((59 <= code && code <= 68 /* F1-F10 */) || code == 87 /* F11 */ || code == 88 /* F12 */)
        return 0x80 + code;

    // Number row
    if (2 <= code && code <= 11)
        return keyboard_nums[code - 2];

    // QWERTY row
    if (16 <= code && code <= 27)
        return keyboard_qwerty[code - 16];

    // ASDF row
    if (30 <= code && code <= 40)
        return keyboard_asdf[code - 30];

    // ZXCV row
    if (43 <= code && code <= 53)
        return keyboard_zxcv[code - 43];

    // Misc keys
    switch (code) {
    case 1:
        return KEY_ESCAPE;
    case 14:
        return KEY_BACKSPACE;
    case 15:
        return KEY_TAB;
    case 28:
        return KEY_ENTER;
    case 57:
        return KEY_USE;
    case 12:
        return KEY_MINUS;
    case 13:
        return KEY_EQUALS;
    case 29:
    case 97:
        return KEY_FIRE; // left/right ctrl
    case 42:
    case 54:
        return KEY_RSHIFT; // left/right shift
    case 56:
    case 100:
        return KEY_LALT; // left/right alt
    default:
        return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    input_req_t req = { .op = INPUT_OP_GET };
    ssend(&req, sizeof req, INPUT_SERVER);
    input_resp_t resp;
    recv(&resp, sizeof resp, INPUT_SERVER);
    if (!resp.has_event)
        return 0;
    // TODO translate mouse left/right to arrow keys
    if (resp.type != 1) // not EV_KEY; consume without reporting
        return 0;
    *pressed = resp.value;
    *doomKey = toDoomKey(resp.code);
    return 1;
}

void DG_SetWindowTitle(const char *title) { printf("DG_SetWindowTitle: %f", title); }

int main() {
    doomgeneric_Create(0, NULL);
    for (;;)
        doomgeneric_Tick();
}
