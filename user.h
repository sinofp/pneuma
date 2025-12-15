#pragma once
#include "common.h"

int readfile(unsigned long, char *, size_t);
int writefile(unsigned long, const char *, size_t);
int yield();

extern "C" {

[[noreturn]]
void exit(void);
}
