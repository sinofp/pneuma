#pragma once

#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *__restrict__, const void *__restrict__, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *); // TODO replace by strlcpy
int strcmp(const char *, const char *);
size_t strlen(const char *);

#ifdef __cplusplus
}
#endif
