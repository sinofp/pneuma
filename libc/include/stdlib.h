#pragma once

template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

extern "C" {

[[noreturn]]
void exit(int);

}
