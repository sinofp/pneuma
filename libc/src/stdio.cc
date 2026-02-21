#include "ff.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>

// vaddr_t equivalent for user space
using vaddr_t = uintptr_t;

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

// TODO: Implement these for Doom
int fprintf(FILE *stream, const char *format, ...) {
    (void) stream;
    (void) format;
    return 0;
}

int sprintf(char *str, const char *format, ...) {
    (void) str;
    (void) format;
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    (void) stream;
    (void) format;
    (void) ap;
    return 0;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    (void) str;
    (void) size;
    (void) format;
    (void) ap;
    return 0;
}

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
