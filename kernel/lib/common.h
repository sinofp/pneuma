#pragma once

#include "lib/types.h"

#define va_list                  __builtin_va_list
#define va_start                 __builtin_va_start
#define va_end                   __builtin_va_end
#define va_arg                   __builtin_va_arg

#define align_up(value, align)   __builtin_align_up(value, align)
#define align_down(value, align) __builtin_align_down(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

// TODO concept?
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

extern "C" {

// TODO use span
void *memcpy(void *__restrict__, const void *__restrict__, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *); // TODO replace by strlcpy
int strcmp(const char *, const char *);
size_t strlen(const char *);
void printf(const char *__restrict__, ...);
void printf_unlocked(const char *__restrict__, ...);

// Below functions are implemented in kernel using OpenSBI
extern int putchar(int);
}

constexpr int EOF = -1;
