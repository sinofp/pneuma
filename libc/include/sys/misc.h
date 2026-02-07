#pragma once

#include <stddef.h> // size_t

int readfile(unsigned long fd, char *buf, size_t len);
int writefile(unsigned long fd, const char *buf, size_t len);
int sleep_ms(unsigned ms);
