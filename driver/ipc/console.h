#pragma once

#include <stdbool.h> // For C
#include <stdint.h>

#define CONSOLE_SERVER   5

// This bounds two different things, and they pull in opposite directions.
//
// Upward: it is the longest line that stays atomic. Anything longer is split across several
// messages, and another process's output can land in the gap. Only userspace reaches this protocol,
// so only userspace lines count: the longest literal there expands to roughly 130 (doomgeneric's
// I_InitGraphics line), while runtime content is what actually reaches 256, e.g. printf("%s", path)
// with FS_MAX_PATH. The kernel's own printf never comes through here; it goes to SBI directly.
//
// Downward: it is how long the server can be unavailable. The server pushes a whole message onto
// the wire before returning to recv, so at 115200 a full 256 bytes takes 22 ms, during which every
// printf and every getchar poll in the system blocks. 1024 would make that 89 ms, and blk's 4 KB
// would make it 356 ms.
//
// A client sends offsetof(console_req_t, data) + len bytes rather than the whole struct, so the
// maximum costs nothing per message. It costs one static buffer per client, and the stall above.
#define CONSOLE_MAX_DATA 256

#define CONSOLE_OP_WRITE 1
#define CONSOLE_OP_READ  2

typedef struct {
    int op;
    uint32_t len;                // WRITE only: bytes in data[]. See the clamp note below.
    char data[CONSOLE_MAX_DATA]; // WRITE only
} console_req_t;

typedef struct {
    // To keep a READ from blocking the server: the server also
    // serves TX for every other process, so spinning here on the RX FIFO freezes every printf in the
    // system, and each keystroke then releases one batch of everyone else's output. The waiting
    // belongs in the caller, which polls and sleeps between tries.
    bool has_char;
    char ch;
} console_resp_t;
