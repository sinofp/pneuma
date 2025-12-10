#include "common.h"

extern int sbi_debug_console_write(const char *);

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
    // TODO test 128 and 0
    return *s1 - *s2;
}

namespace {
template <typename T> void print_hex(T value) {
    // If i is auto (size_t), --i will also be positive
    for (int i = sizeof(T) * 2 - 1; i >= 0; --i) {
        uint_fast8_t nibble = value >> (i * 4) & 0xf;
        putchar("0123456789abcdef"[nibble]);
    }
}

template <typename T> void print_dec(T value) {
    // https://github.com/nuta/operating-system-in-1000-lines/issues/64
    __make_unsigned(T) magnitude = value;
    if (value < 0) {
        putchar('-');
        magnitude = -magnitude;
    }

    decltype(magnitude) divisor = 1;
    while (magnitude / divisor > 9)
        divisor *= 10;

    while (divisor) {
        putchar('0' + magnitude / divisor);
        magnitude %= divisor;
        divisor /= 10;
    }
}
} // namespace

void printf(const char *__restrict__ format, ...) {
    va_list vargs;
    va_start(vargs, format);

    while (*format) {
        if (*format == '%') {
            switch (*++format) { // Skip '%', read the next character
            case '\0':           // '%' at the end of the format string
                putchar('%');
                goto end;
            case '%': // Print '%'
                putchar('%');
                break;
            case 's': {
                const auto *s = va_arg(vargs, const char *);
                for (auto i = 0uz; s[i] != '\0'; ++i)
                    putchar(s[i]);
                break;
            }
            case 'd':
                print_dec(va_arg(vargs, int));
                break;
            case 'x':
                print_hex(va_arg(vargs, unsigned));
                break;
            case 'p':
                putchar('0');
                putchar('x');
                // %p for pointers (typically virtual addresses), so we convert (void *) to vaddr_t for arthimetict,
                // and use %lx for printing physicall addresses for (the last 32-bit of) Sv32 and Sv39.
                print_hex(reinterpret_cast<vaddr_t>(va_arg(vargs, void *)));
                break;
            case 'l':
                switch (*++format) {
                case 'd':
                    print_dec(va_arg(vargs, long));
                    break;
                case 'x':
                    print_hex(va_arg(vargs, unsigned long));
                    break;
                case 'l':
                    switch (*++format) {
                    // Don't work on RV32 because 64-bit division
                    // case 'd':
                    //     print_dec(va_arg(vargs, long long));
                    //     break;
                    case 'x':
                        print_hex(va_arg(vargs, unsigned long long));
                        break;
                        break;
                    }
                    break;
                default:
                }
                break;
            default:
            }
        } else
            putchar(*format);

        ++format;
    }

end:
    va_end(vargs);
}
