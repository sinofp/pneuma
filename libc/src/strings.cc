#include <stddef.h>
#include <strings.h>

namespace {
// should be in ctype.h
int tolower(int c) {
    if ('A' <= c and c < 'Z')
        return c + 'a' - 'A';
    return c; // \0 returns \0
}
} // namespace

int strcasecmp(const char *s1, const char *s2) {
    for (; *s1 and *s2 and tolower(*s1) == tolower(*s2); ++s1, ++s2) {
    }
    return static_cast<unsigned char>(tolower(*s1)) - static_cast<unsigned char>(tolower(*s2));
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    for (; n and *s1 and *s2 and tolower(*s1) == tolower(*s2); --n, ++s1, ++s2) {
    }
    return n ? (static_cast<unsigned char>(tolower(*s1)) - static_cast<unsigned char>(tolower(*s2))) : 0;
}
