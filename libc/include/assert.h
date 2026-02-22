#pragma once

// Minimal assert.h stub for doomgeneric

#ifdef NDEBUG
#define assert(x) ((void) 0)
#else
#define assert(x) ((void) ((x) || (__assert_fail(#x, __FILE__, __LINE__), 0)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __assert_fail(const char *expr, const char *file, int line);

#ifdef __cplusplus
}
#endif
