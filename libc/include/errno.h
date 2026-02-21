#pragma once

extern int errno;

enum errno_t : int {
    ENOMEM = 12, // Cannot allocate memory
    EINVAL = 22, // Invalid argument
};
