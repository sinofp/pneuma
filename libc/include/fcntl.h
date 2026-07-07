#pragma once

// Minimal fcntl.h stub for doomgeneric

#ifdef __cplusplus
extern "C" {
#endif

// File access modes
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

// File creation flags
#define O_CREAT  0x0100
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#ifdef __cplusplus
}
#endif
