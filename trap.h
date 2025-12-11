#pragma once

#include "kernel.h"

void interrupt_init();

using sxlen_t = unsigned long;

struct [[gnu::packed]] trap_frame {
    xlen_t ra;
    xlen_t gp;
    xlen_t tp;
    xlen_t t0;
    xlen_t t1;
    xlen_t t2;
    xlen_t t3;
    xlen_t t4;
    xlen_t t5;
    xlen_t t6;
    xlen_t a0;
    xlen_t a1;
    xlen_t a2;
    xlen_t a3;
    xlen_t a4;
    xlen_t a5;
    xlen_t a6;
    xlen_t a7;
    xlen_t s0;
    xlen_t s1;
    xlen_t s2;
    xlen_t s3;
    xlen_t s4;
    xlen_t s5;
    xlen_t s6;
    xlen_t s7;
    xlen_t s8;
    xlen_t s9;
    xlen_t s10;
    xlen_t s11;
    xlen_t sp;
};
