#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int putchar(int);
int getchar();
void printf(const char *__restrict__, ...);

#ifdef __cplusplus
}
#endif

constexpr int EOF = -1;
