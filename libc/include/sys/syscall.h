#pragma once

#include "shared.h"

// Raw syscall interface
#ifdef __cplusplus
extern "C" {
#endif

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2);

#ifdef __cplusplus
}
#endif
