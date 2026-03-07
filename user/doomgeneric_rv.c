// Doomgeneric platform layer for RISC-V bare-metal OS
// Stub implementations - no display or input yet

#include "doomgeneric.h"
#include "m_fixed.h"
#include <limits.h>
#include <stdlib.h>
#include <sys/misc.h>

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
    // Platform-specific initialization (none needed for now)
}

void DG_DrawFrame(void) {
    // Stub: no display yet
}

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    // TODO: implement real tick counter via syscall
    return 0;
}

int DG_GetKey(int *pressed, unsigned char *key) {
    (void)pressed;
    (void)key;
    return 0; // No input
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
    // No-op
}

// Entry point
void doomgeneric_Create(int argc, char **argv);

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    return 0;
}
