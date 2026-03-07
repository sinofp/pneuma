#pragma once

#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

int abs(int);

double atof(const char *);

int atoi(const char *);

void *calloc(size_t, size_t);

void free(void *);

void *malloc(size_t);

void *realloc(void *, size_t);

int system(const char *);

[[noreturn]]
void exit(int);

#ifdef __cplusplus
}
#endif
