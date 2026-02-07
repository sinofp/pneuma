#include <stdint.h>
#include <string.h>

void *memcpy(void *__restrict__ dst, const void *__restrict__ src, size_t n) {
    auto *d = static_cast<uint8_t *>(dst);
    auto *s = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst; // C Manual: 'Returns the original value of dst'
}

void *memset(void *b, int c, size_t len) {
    auto *p = static_cast<uint8_t *>(b);
    while (len--)
        *p++ = static_cast<unsigned char>(c); // C standard historical design
    return b;                                 // C Manual: 'Returns its first argument'
}

char *strcpy(char *dst, const char *src) {
    size_t i;
    for (i = 0; src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

int strcmp(const char *s1, const char *s2) {
    for (; *s1 and *s2 and *s1 == *s2; ++s1, ++s2) {
    }
    // C Manual: 'The comparison is done using unsigned characters'
    // Calling Convention: 'The C types char and unsigned char are 8-bit unsigned integers and are zero-extended when
    // stored in a RISC-V integer register.' Therefore we don't need to cast on RISC-V.
    return *s1 - *s2;
}

size_t strlen(const char *s) {
    size_t i;
    for (i = 0; '\0' != s[i]; ++i) {
    }
    return i;
}
