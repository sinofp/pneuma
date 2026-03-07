#include <ctype.h>

int isprint(int c) { return 040 <= c and c <= 077 or 0100 <= c and c <= 0176; }

int isspace(int c) { return ' ' == c or '\t' == c or '\n' == c or '\v' == c or '\f' == c or '\r' == c; }

int toupper(int c) {
    if ('a' <= c and c <= 'z')
        return c - 'a' + 'A';
    return c;
}
