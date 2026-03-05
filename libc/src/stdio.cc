#include "ff.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // malloc
#include <string.h> // strlen
#include <sys/syscall.h>

// vaddr_t equivalent for user space
using vaddr_t = uintptr_t;

// Standard streams (stubs - point to nothing for now)
FILE *stdout = nullptr;
FILE *stderr = nullptr;

int putchar(int ch) { return syscall(SYS_PUTCHAR, ch, 0, 0); }

int getchar() { return syscall(SYS_GETCHAR, 0, 0, 0); }

namespace {

// FatFs state
FATFS fatfs;
bool mounted = false;

// FIL pool for open files
constexpr size_t MAX_OPEN_FILES = 16;
FIL fil_pool[MAX_OPEN_FILES];
bool fil_in_use[MAX_OPEN_FILES];

FIL *alloc_fil() {
    for (size_t i = 0; i < MAX_OPEN_FILES; ++i)
        if (!fil_in_use[i]) {
            fil_in_use[i] = true;
            return &fil_pool[i];
        }
    return NULL;
}

void free_fil(FIL *f) {
    for (size_t i = 0; i < MAX_OPEN_FILES; ++i)
        if (&fil_pool[i] == f) {
            fil_in_use[i] = false;
            return;
        }
}

bool ensure_mounted() {
    if (mounted)
        return true;

    FRESULT res = f_mount(&fatfs, "", 1);
    if (FR_OK != res) {
        printf("fatfs: mount failed with error %d\n", res);
        return false;
    }
    mounted = true;
    return true;
}

BYTE mode_to_flags(const char *mode) {
    if (nullptr == mode)
        return 0;

    bool read = false, write = false, append = false, create = false;

    for (const char *p = mode; '\0' != *p; ++p) {
        switch (*p) {
        case 'r':
            read = true;
            break;
        case 'w':
            write = true;
            create = true;
            break;
        case 'a':
            write = true;
            append = true;
            create = true;
            break;
        case '+':
            read = true;
            write = true;
            break;
        }
    }

    BYTE flags = 0;
    if (read and !write)
        flags = FA_READ;
    else if (!read and write)
        flags = FA_WRITE;
    else if (read and write)
        flags = FA_READ | FA_WRITE;

    if (create)
        flags |= FA_CREATE_ALWAYS;
    if (append)
        flags |= FA_OPEN_APPEND;
    if (read and !create and !append)
        flags = FA_READ | FA_OPEN_EXISTING;

    return flags;
}

} // namespace

// File operations

FILE *fopen(const char *path, const char *mode) {
    if (nullptr == path or nullptr == mode)
        return NULL;

    if (!ensure_mounted())
        return NULL;

    auto *fil = alloc_fil();
    if (nullptr == fil) {
        printf("fopen: no free FIL handles\n");
        return NULL;
    }

    BYTE flags = mode_to_flags(mode);
    if (0 == flags) {
        free_fil(fil);
        return NULL;
    }

    FRESULT res = f_open(fil, path, flags);
    if (FR_OK != res) {
        free_fil(fil);
        return NULL;
    }

    return reinterpret_cast<FILE *>(fil);
}

int fclose(FILE *stream) {
    if (nullptr == stream)
        return EOF;

    auto *fil = reinterpret_cast<FIL *>(stream);
    FRESULT res = f_close(fil);
    free_fil(fil);
    return (FR_OK == res) ? 0 : EOF;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (nullptr == stream or nullptr == ptr or 0 == size or 0 == nmemb)
        return 0;

    auto *fil = reinterpret_cast<FIL *>(stream);
    UINT br;
    FRESULT res = f_read(fil, ptr, size * nmemb, &br);

    return (FR_OK == res) ? br / size : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (nullptr == stream or nullptr == ptr or 0 == size or 0 == nmemb)
        return 0;

    auto *fil = reinterpret_cast<FIL *>(stream);
    UINT bw;
    FRESULT res = f_write(fil, ptr, size * nmemb, &bw);

    return (FR_OK == res) ? bw / size : 0;
}

int fseek(FILE *stream, long offset, int whence) {
    if (nullptr == stream)
        return -1;

    auto *fil = reinterpret_cast<FIL *>(stream);
    FSIZE_t new_pos;

    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = f_tell(fil) + offset;
        break;
    case SEEK_END:
        new_pos = f_size(fil) + offset;
        break;
    default:
        return -1;
    }

    return (FR_OK == f_lseek(fil, new_pos)) ? 0 : -1;
}

long ftell(FILE *stream) {
    if (nullptr == stream)
        return -1;

    auto *fil = reinterpret_cast<FIL *>(stream);
    return static_cast<long>(f_tell(fil));
}

int fflush(FILE *stream) {
    if (nullptr == stream)
        return EOF;

    auto *fil = reinterpret_cast<FIL *>(stream);
    return (FR_OK == f_sync(fil)) ? 0 : EOF;
}

int feof(FILE *stream) {
    if (nullptr == stream)
        return 0;

    auto *fil = reinterpret_cast<FIL *>(stream);
    return f_eof(fil) ? 1 : 0;
}

char *fgets(char *s, int size, FILE *stream) {
    if (nullptr == s or size <= 0 or nullptr == stream)
        return NULL;

    auto *fil = reinterpret_cast<FIL *>(stream);
    int i = 0;
    while (i < size - 1) {
        unsigned char c;
        UINT br;
        if (FR_OK != f_read(fil, &c, 1, &br) or 0 == br)
            break;
        s[i++] = static_cast<char>(c);
        if ('\n' == c)
            break;
    }

    if (0 == i)
        return NULL;

    s[i] = '\0';
    return s;
}

int puts(const char *s) {
    if (nullptr == s)
        return EOF;

    while ('\0' != *s)
        putchar(*s++);
    putchar('\n');
    return 0;
}

void perror(const char *s) {
    if (nullptr != s and '\0' != *s) {
        printf("%s: ", s);
    }
    printf("error\n");
}

// printf family

namespace {

struct nullopt_t {};

constexpr nullopt_t nullopt;

template <typename T> class optional {
    // T is always constructed, this is wrong, but fine because we only use int types
    T v;
    bool has;

public:
    // I don't allow create an empty option this way
    constexpr optional() noexcept = delete;

    constexpr optional(nullopt_t) noexcept
        : v{}
        , has{ false } {}

    optional(T v_)
        : v{ v_ }
        , has{ true } {}

    constexpr explicit operator bool() const noexcept { return has; }

    constexpr T &value() & { return v; }
};

template <typename T> T max(T a, T b) { return a < b ? b : a; }

template <typename T> T min(T a, T b) { return a < b ? a : b; }

template <typename U, typename V> struct pair {
    U first;
    V second;
};

template <typename T> pair<size_t, T> width_divisor(T value, T base = 10) {
    size_t width = 1;
    T divisor = 1;
    for (; base <= value / divisor; ++width, divisor *= base) {
    }
    return { width, divisor };
}

void print_padding(size_t width, bool minus, auto print_char, char padding = ' ',
                   optional<size_t> minimum_width = nullopt, optional<size_t> int_presicion = nullopt) {
    size_t spaces = 0, zeros = 0;
    if (int_presicion and width < int_presicion.value())
        zeros = int_presicion.value() - width;

    // int_presicion only cares the digits, but width cares the sign
    width += minus;
    if (!int_presicion and '0' == padding and minimum_width and width < minimum_width.value())
        zeros = minimum_width.value() - width;

    width += zeros;
    if ((' ' == padding or int_presicion /* ignore zero padding -> always space padding */) and minimum_width and
        width < minimum_width.value())
        spaces = minimum_width.value() - width;

    while (spaces--)
        print_char(' ');
    if (minus)
        print_char('-');
    while (zeros--)
        print_char('0');
}

template <typename T>
void print_hex(T value, auto print_char, char padding = ' ', optional<size_t> minimum_width = nullopt,
               optional<size_t> precision = nullopt) {
    auto [width, _]{ width_divisor(value, { 16 }) };

    print_padding(width, false, print_char, padding, minimum_width, precision);

    for (auto i = width; 0 < i--;) {
        uint_fast8_t nibble = value >> (i * 4) & 0xf;
        print_char("0123456789abcdef"[nibble]);
    }
}

template <typename T>
void print_dec(T value, auto print_char, char padding = ' ', optional<size_t> minimum_width = nullopt,
               optional<size_t> precision = nullopt) {

    bool minus = value < 0;

    // https://github.com/nuta/operating-system-in-1000-lines/issues/64
    __make_unsigned(T) magnitude = value;
    if (minus)
        magnitude = -magnitude;

    auto [width, divisor]{ width_divisor(magnitude) };

    print_padding(width, minus, print_char, padding, minimum_width, precision);

    if (precision and !precision.value() and !value) {
        // special case: if both are zero, don't print any digit
        print_char(' ');
        return;
    }

    while (divisor) {
        print_char('0' + magnitude / divisor);
        magnitude %= divisor;
        divisor /= 10;
    }
}

void print_double(double value, auto print_char, char padding = ' ', optional<size_t> minimum_width = nullopt,
                  optional<size_t> precision = nullopt) {
    bool minus = value < 0;
    if (minus)
        value = -value;

    uintptr_t integer = value;
    double fractional = value - integer;

    auto [integer_width, integer_divisor]{ width_divisor(integer) };

    size_t fractional_width = precision ? precision.value() : 6;

    auto width = integer_width + (fractional_width ? (1 /* dot */ + fractional_width) : 0);
    print_padding(width, minus, print_char, padding, minimum_width, nullopt);

    for (; integer_divisor; integer %= integer_divisor, integer_divisor /= 10)
        print_char('0' + integer / integer_divisor);

    if (fractional_width)
        print_char('.');

    for (auto i = 0uz; i < fractional_width; ++i) {
        fractional *= 10;
        unsigned digit = fractional;
        fractional -= digit;
        print_char('0' + digit);
    }
}

void print_string(const char *s, auto print_char, char padding = ' ', optional<size_t> minimum_width = nullopt,
                  optional<size_t> precision = nullopt) {
    auto n = strlen(s);
    if (precision)
        n = min(precision.value(), n);
    if (minimum_width and n < minimum_width.value())
        for (auto i = 0uz; i < (minimum_width.value() - n); ++i)
            print_char(padding);

    for (auto i = 0uz; i < n; ++i)
        print_char(s[i]);
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
            case 's':
                print_string(va_arg(vargs, const char *), putchar);
                break;
            case 'd':
                print_dec(va_arg(vargs, int), putchar);
                break;
            case 'x':
                print_hex(va_arg(vargs, unsigned), putchar);
                break;
            case 'p':
                putchar('0');
                putchar('x');
                // %p for pointers (typically virtual addresses), so we convert (void *) to vaddr_t for arithmetic,
                // and use %lx for printing physical addresses for (the last 32-bit of) Sv32 and Sv39.
                print_hex(reinterpret_cast<vaddr_t>(va_arg(vargs, void *)), putchar);
                break;
            case 'l':
                switch (*++format) {
                case 'd':
                    print_dec(va_arg(vargs, long), putchar);
                    break;
                case 'x':
                    print_hex(va_arg(vargs, unsigned long), putchar);
                    break;
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

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vfprintf(stream, format, ap);
    va_end(ap);
    return res;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    // Get the required length to allocate
    va_list ap2;
    va_copy(ap2, ap);
    auto n = vsnprintf(nullptr, 0, format, ap2);

    auto *s = static_cast<char *>(malloc(n));
    vsnprintf(s, n, format, ap);
    fwrite(s, 1, n, stream);
    free(s);
    return 0;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

optional<char> pLiteral(const char *&format, char ch) {
    if (ch == *format)
        return { *(format++) };
    return nullopt;
}

template <typename T> optional<T> pDecimal(const char *&format) {
    T i = 0;
    bool consumed = false;
    while ('0' <= *format and *format <= '9') {
        i *= 10;
        i += *(format++) - '0';
        consumed = true;
    }
    return consumed ? optional{ i } : nullopt;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    // max size seems to be 80 in doomgeneric
    --size;
    size_t would = 0, written = 0;

    auto putchar1 = [&would, &written, str, size](char ch) {
        if (written != size)
            str[written++] = ch;
        ++would;
    };

    // %i %02i %3i %7i %s %.8s %d %.2d %2.2d %x %02x %p %f %% %c
    while (*format) {
        if ('%' == *format) {
            // The only flag doomgeneric uses is zero padding
            auto zero_padding{ pLiteral(++format, '0') };
            auto width{ pDecimal<size_t>(format) };
            auto dot{ pLiteral(format, '.') };
            auto precision{ pDecimal<size_t>(format) };
            if (dot and !precision)
                precision = { 0 };
            // doomgeneric doesn't use length modifier
            switch (*format++) {
            case 'i':
            case 'd':
                print_dec(va_arg(ap, int), putchar1, zero_padding ? zero_padding.value() : ' ', width, precision);
                break;
            case 'x':
                print_hex(va_arg(ap, unsigned), putchar1, zero_padding ? zero_padding.value() : ' ', width, precision);
                break;
            case 'p':
                putchar1('0');
                putchar1('x');
                // %p for pointers (typically virtual addresses), so we convert (void *) to vaddr_t for arithmetic,
                // and use %lx for printing physical addresses for (the last 32-bit of) Sv32 and Sv39.
                print_hex(reinterpret_cast<vaddr_t>(va_arg(ap, void *)), putchar1,
                          zero_padding ? zero_padding.value() : ' ', width, precision);
                break;
            case 'f':
                print_double(va_arg(ap, double), putchar1, zero_padding ? zero_padding.value() : ' ', width, precision);
                break;
            case 'c':
                putchar1(va_arg(ap, int));
                break;
            case 's':
                print_string(va_arg(ap, const char *), putchar1, zero_padding ? zero_padding.value() : ' ', width,
                             precision);
                break;
            case '%':
                putchar1('%');
                break;
            default:
            }
        } else
            putchar1(*format++);
    }

    str[written] = '\0';
    return would;
}

// %x %o %d %i 79s %99[^\n]

int fscanf(FILE *stream, const char *format, ...) {
    (void) stream;
    (void) format;
    return 0;
}

int sscanf(const char *str, const char *format, ...) {
    (void) str;
    (void) format;
    return 0;
}
