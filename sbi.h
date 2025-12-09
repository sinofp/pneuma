#pragma once
#include "kernel.h"

struct sbiret {
    long error;

    union {
        unsigned long uvalue;
        long value;
    };
};

constexpr decltype(sbiret::error) SBI_SUCCESS = 0;

constexpr xlen_t SBI_EID_DBCN = 0x4442434E;
