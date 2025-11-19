#pragma once

struct sbiret {
    long error;
    union {
        long value;
	unsigned long uvalue;
    };
};

