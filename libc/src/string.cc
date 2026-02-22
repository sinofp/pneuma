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

int memcmp(const void *s1, const void *s2, size_t n) {
    // C Manual: compares byte string s1 against byte string s2
    // I think byte is identical to char, but let's be explicit and direct
    auto *p1 = static_cast<const uint8_t *>(s1);
    auto *p2 = static_cast<const uint8_t *>(s2);
    for (size_t i = 0; i < n; ++i)
        if (p1[i] != p2[i])
            // C Manual: treated as unsigned char values, so that ‘\200’ is greater than ‘\0’, for example
            // We don't cast char to unsigned char in strcmp, but uint8_t and unsigned char has semantic difference here
            return static_cast<unsigned char>(p1[i]) - static_cast<unsigned char>(p2[i]);
    return 0;
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

char *strchr(const char *s, int c) {
    for (; '\0' != *s; ++s)
        if (*s == static_cast<char>(c))
            // Returning a char* out of a const char* is very bizarre
            return const_cast<char *>(s);

    // C Manual: if c is ‘\0’, the functions locate the terminating ‘\0’
    return ('\0' == c) ? const_cast<char *>(s) : NULL;
}

// TODO: Implement for Doom
void *memmove(void *dst, const void *src, size_t n) {
    (void) dst;
    (void) src;
    (void) n;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    (void) dst;
    (void) src;
    (void) n;
    return dst;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    (void) s1;
    (void) s2;
    (void) n;
    return 0;
}

char *strrchr(const char *s, int c) {
    (void) s;
    (void) c;
    return NULL;
}

char *strstr(const char *haystack, const char *needle) {
    (void) haystack;
    (void) needle;
    return NULL;
}

char *strdup(const char *s) {
    (void) s;
    return NULL;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    (void) str;
    (void) size;
    (void) format;
    return 0;
}
