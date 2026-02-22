#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern int errno;

#define ENOMEM 12 // Cannot allocate memory
#define EISDIR 21 // Is a directory
#define EINVAL 22 // Invalid argument

#ifdef __cplusplus
}
#endif
