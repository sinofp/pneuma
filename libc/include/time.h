#pragma once

// #include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

// For peanut_gb.h
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

#ifdef __cplusplus
}
#endif
