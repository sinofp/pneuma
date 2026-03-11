// Doomgeneric platform layer for RISC-V bare-metal OS
// Stub implementations - no display or input yet

#include "doomgeneric.h"
#include "m_fixed.h"
#include <limits.h>
#include <stdlib.h>
#include <sys/misc.h>
#include <sys/syscall.h>

// Use double to fake Doom 16.16 fixed-point to replace 64-bit int divison on RV32
fixed_t FixedMul(fixed_t a, fixed_t b) {
    return (fixed_t)((double)a * b / FRACUNIT);
}

fixed_t FixedDiv(fixed_t a, fixed_t b) {
    if ((abs(a) >> 14) >= abs(b))
        return (a ^ b) < 0 ? INT_MIN : INT_MAX;
    return (fixed_t)((double)a / b * FRACUNIT);
}

// DG_ScreenBuffer is defined and allocated in doomgeneric.c

void DG_Init(void) {
    // puts("DG_Init");
    syscall(SYS_GPU_ATTACH, (unsigned long)DG_ScreenBuffer, 0, 0);
}

void DG_DrawFrame(void) {
    // puts("DG_DrawFrame");
    syscall(SYS_GPU_FLUSH, 0, 0, 0);
}

void DG_SleepMs(uint32_t ms) {
    // puts("DG_SleepMs");
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    // puts("DG_GetTicksMs");
    return syscall(SYS_GET_TICKS, 0, 0, 0);
}

int DG_GetKey(int *pressed, unsigned char *key) {
    // puts("DG_GetKey");
    (void)pressed;
    (void)key;
    return 0; // No input
}

void DG_SetWindowTitle(const char *title) {
    puts("DG_WindowTitle");
    (void)title;
    // No-op
}

// Entry point
void doomgeneric_Create(int argc, char **argv);

int main() {
    doomgeneric_Create(0, NULL);
    return 0;
}
