#pragma once

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic console I/O
int putchar(int);
int getchar();
void printf(const char *__restrict__, ...);
int puts(const char *s);
void perror(const char *s);

// FILE is opaque (internally FIL*)
typedef void FILE;

// File operations
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
char *fgets(char *s, int size, FILE *stream);

// Formatted I/O (stubs for Doom)
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);

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
