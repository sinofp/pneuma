#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void *memcpy(void *__restrict__ dst, const void *__restrict__ src, size_t n) {
    auto *d = static_cast<uint8_t *>(dst);
    auto *s = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst; // C Manual: 'Returns the original value of dst'
}

void *memmove(void *dst, const void *src, size_t n) {
    // C Manual: The two strings may overlap; the copy is always done in a non-destructive manner.
    auto *dst_p = static_cast<char *>(dst);
    auto *src_p = static_cast<const char *>(src);
    // Case 2:
    //         | dst |
    //    | src |
    if (src_p < dst_p and dst_p < src_p + n) {
        for (; 0 < n-- /* Check before decrement, to avoid size_t underflow */;)
            dst_p[n] = src_p[n];
        return dst;
    }
    // Case 1:
    //         | dst |
    //             | src |
    // Case 3: no overlap
    // They're the same: start from beginning
    return memcpy(dst, src, n);
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
    for (i = 0; '\0' != src[i]; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; n and '\0' != src[i]; --n, ++i)
        dst[i] = src[i];
    // If src is less than len characters long, the remainder of dst is filled with ‘\0’ characters.  Otherwise, dst is
    // not terminated.
    for (; n; --n, ++i)
        dst[i] = '\0';
    return dst;
}

char *strdup(const char *s) {
    if (auto *p = static_cast<char *>(malloc(1 /* for \0 */ + strlen(s)))) {
        strcpy(p, s);
        return p;
    }
    // malloc sets errno to ENOMEM if returns NULL
    return NULL;
}

int strcmp(const char *s1, const char *s2) {
    for (; *s1 and *s2 and *s1 == *s2; ++s1, ++s2) {
    }
    // C Manual: 'The comparison is done using unsigned characters'
    // Calling Convention: 'The C types char and unsigned char are 8-bit unsigned integers and are zero-extended when
    // stored in a RISC-V integer register.' Therefore we don't need to cast on RISC-V.
    return *s1 - *s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (; n and *s1 and *s2 and *s1 == *s2; --n, ++s1, ++s2) {
    }
    // In strcmp, we always return this, because if the two strings are the same, their next chars are also the same \0
    // However, we have n in strncmp, so we only compare the next chars if n is not exhausted
    return n ? (*s1 - *s2) : 0;
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

char *strrchr(const char *s, int c) {
    auto len = strlen(s);
    for (auto i = strlen(s) /* the first s[i] is \0 on purpose */ + 1 /* +1 for first -- */;
         0 < i-- /* similar to the reverse case in memmove */;) {
        if (s[i] == static_cast<char>(c))
            return const_cast<char *>(s + i);
    }
    return NULL;
}

char *strstr(const char *haystack, const char *needle) {
    // C Manual: If needle is an empty string, haystack is returned
    if (!needle or '\0' == *needle)
        return const_cast<char *>(haystack);

    // TODO https://oi-wiki.org/string/bm/

    const auto needle_len = strlen(needle);

    while ('\0' != *haystack) {
        if (!strncmp(haystack, needle, needle_len))
            return const_cast<char *>(haystack);
        ++haystack;
    }

    return NULL;
}
