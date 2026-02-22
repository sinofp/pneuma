#pragma once

// Minimal sys/stat.h stub for doomgeneric

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t mode_t;

int mkdir(const char *path, mode_t mode);

#ifdef __cplusplus
}
#endif
