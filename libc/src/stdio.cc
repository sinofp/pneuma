#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>

// vaddr_t equivalent for user space
using vaddr_t = uintptr_t;

int putchar(int ch) { return syscall(SYS_PUTCHAR, ch, 0, 0); }

int getchar() { return syscall(SYS_GETCHAR, 0, 0, 0); }

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
                // %p for pointers (typically virtual addresses), so we convert (void *) to vaddr_t for arithmetic,
                // and use %lx for printing physical addresses for (the last 32-bit of) Sv32 and Sv39.
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
