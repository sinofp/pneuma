#pragma once

#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);

#ifdef __cplusplus
}
#endif
