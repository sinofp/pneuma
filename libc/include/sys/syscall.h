#pragma once

// Syscall numbers
// Technically, they should be xlen_t, because they need to fit in a0
// However, we don't want to expose xlen_t to user applications.
constexpr unsigned long SYS_PUTCHAR = 1;
constexpr unsigned long SYS_GETCHAR = 2;
constexpr unsigned long SYS_EXIT = 3;
constexpr unsigned long SYS_READFILE = 4;
constexpr unsigned long SYS_WRITEFILE = 5;
constexpr unsigned long SYS_SLEEP = 6;

// Raw syscall interface
unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2);
