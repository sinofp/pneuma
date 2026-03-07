#pragma once

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic console I/O
int putchar(int);
int getchar();
int puts(const char *s);

// FILE is opaque (internally FIL*)
typedef void FILE;

// Standard streams (stubs for now)
extern FILE *stdout;
extern FILE *stderr;

// File operations
FILE *fopen(const char *__restrict__, const char *__restrict__);
int fclose(FILE *);
size_t fread(void *__restrict__, size_t, size_t, FILE *__restrict__);
size_t fwrite(const void *__restrict__, size_t, size_t, FILE *__restrict__);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fflush(FILE *);
int feof(FILE *);

int remove(const char *);
int rename(const char *, const char *);

// Formatted I/O
void printf(const char *__restrict__, ...);
int fprintf(FILE *__restrict__, const char *__restrict__, ...);
int snprintf(char *__restrict__, size_t, const char *__restrict__, ...);
int vfprintf(FILE *__restrict__, const char *__restrict__, va_list);
int vsnprintf(char *__restrict__, size_t, const char *__restrict__, va_list);

int sscanf(const char *__restrict__, const char *__restrict__, ...);

// Seek origins
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#define EOF (-1)
#else
constexpr int EOF = -1;
#endif
