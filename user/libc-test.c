#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ASSERT(cond, msg)                                                   \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("ASSERT FAILED: [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

int main(void) {
    // Test malloc
    int *p1 = malloc(sizeof(int) * 10);
    ASSERT(p1, "malloc failed");
    for (int i = 0; i < 10; ++i)
        p1[i] = i * i;
    ASSERT(p1[5] == 25, "p1[5] should be 25");

    // Test calloc
    int *p2 = calloc(10, sizeof(int));
    ASSERT(p2, "calloc failed");
    ASSERT(p2[0] == 0, "p2[0] should be 0 (zeroed by calloc)");
    ASSERT(p2[9] == 0, "p2[9] should be 0 (zeroed by calloc)");

    // Test realloc
    int *p3 = realloc(p1, sizeof(int) * 20);
    ASSERT(p3, "realloc failed");
    ASSERT(p3[5] == 25, "p3[5] should be 25 (preserved from p1)");
    p3[15] = 999;
    ASSERT(p3[15] == 999, "p3[15] should be 999");

    // Test free
    free(p2);
    free(p3);

    // Test allocation after free (coalescing)
    char *p4 = malloc(100);
    ASSERT(p4, "malloc after free failed");
    strcpy(p4, "Hello from libc-test!");
    ASSERT(strcmp(p4, "Hello from libc-test!") == 0, "p4 string mismatch");
    free(p4);

    printf("\n=== Testing stdio FILE* functions ===\n");

    // Test fopen for reading
    FILE *f = fopen("HELLO.TXT", "r");
    ASSERT(f, "fopen(HELLO.TXT, r) failed");

    // Test ftell at start
    long pos = ftell(f);
    ASSERT(0 == pos, "ftell at start should be 0");

    // Test fread
    char buf[64];
    memset(buf, 0, sizeof(buf));
    size_t n = fread(buf, 1, 27, f);
    ASSERT(!strcmp("Hello from FAT filesystem!\n", buf),
           "fread HELLO.TXT should return 'Hello from FAT filesystem!\\n'");

    // Test ftell after read
    pos = ftell(f);
    ASSERT(27 == pos, "ftell after read should be 27");

    // Test fseek to beginning
    int ret = fseek(f, 0, SEEK_SET);
    ASSERT(0 == ret, "fseek to start failed");
    pos = ftell(f);
    ASSERT(0 == pos, "ftell after fseek(0, SEEK_SET) should be 0");

    // Test fseek relative
    ret = fseek(f, 5, SEEK_CUR);
    ASSERT(0 == ret, "fseek(5, SEEK_CUR) failed");
    pos = ftell(f);
    ASSERT(5 == pos, "ftell after fseek(5, SEEK_CUR) should be 5");

    // Test fclose
    ret = fclose(f);
    ASSERT(0 == ret, "fclose failed");

    // Test fopen for writing
    f = fopen("TEST.TXT", "w");
    ASSERT(f, "fopen(TEST.TXT, w) failed");

    // Test fwrite
    const char *msg = "Written by libc-test!";
    n = fwrite(msg, 1, strlen(msg), f);
    ASSERT(n == strlen(msg), "fwrite should return message length");

    ret = fclose(f);
    ASSERT(0 == ret, "fclose after write failed");

    // Verify by reading back
    f = fopen("TEST.TXT", "r");
    ASSERT(f, "fopen(TEST.TXT, r) for verify failed");

    memset(buf, 0, sizeof(buf));
    n = fread(buf, 1, 64, f);
    ASSERT(n == strlen(msg), "fread should return message length");
    ASSERT(0 == strcmp(buf, msg), "Read content should match written content");

    fclose(f);

    // Test mkdir: create directory, write and read a file inside it
    ASSERT(mkdir("/testdir", 0755) == 0, "mkdir");
    ASSERT(mkdir("/testdir", 0755) != 0, "mkdir duplicate fails");

    f = fopen("/testdir/inside.txt", "w");
    ASSERT(f, "fopen inside new dir failed");
    const char *dir_msg = "inside testdir";
    n = fwrite(dir_msg, 1, strlen(dir_msg), f);
    ASSERT(n == strlen(dir_msg), "fwrite inside dir");
    fclose(f);

    f = fopen("/testdir/inside.txt", "r");
    ASSERT(f, "fopen inside dir for read failed");
    memset(buf, 0, sizeof(buf));
    n = fread(buf, 1, sizeof(buf), f);
    ASSERT(n == strlen(dir_msg), "fread inside dir");
    ASSERT(0 == strcmp(buf, dir_msg), "dir file content matches");
    fclose(f);

    printf("=== All stdio tests passed! ===\n");

    printf("\n=== Testing large file I/O ===\n");
    f = fopen("LARGE.TXT", "w");
    ASSERT(f, "fopen(LARGE.TXT, w) failed");

    char large_buf[4096];
    for (int i = 0; i < 4096; i++)
        large_buf[i] = 'A' + (i % 26);

    n = fwrite(large_buf, 1, 4096, f);
    ASSERT(n == 4096, "fwrite 4096 failed");
    fclose(f);

    f = fopen("LARGE.TXT", "r");
    ASSERT(f, "fopen(LARGE.TXT, r) for verify failed");

    char verify[4096];
    n = fread(verify, 1, 4096, f);
    ASSERT(n == 4096, "fread 4096 failed");

    int verify_ok = 1;
    for (int i = 0; i < 4096; i++) {
        if (verify[i] != large_buf[i]) {
            verify_ok = 0;
            break;
        }
    }
    ASSERT(verify_ok, "verify failed");
    fclose(f);
    printf("=== Large file test passed ===\n");

    printf("\n=== Testing string functions ===\n");

    // memmove: dst is uninitialised — strlen(dst) would run off the buffer
    // and fault or return garbage. memmove must not call strlen on its arguments.
    char *uninit = (char *) malloc(5);
    memmove(uninit, "Hello", 5);
    ASSERT(memcmp(uninit, "Hello", 5) == 0, "memmove into uninitialised dst");
    free(uninit);

    // memmove: non-overlapping
    char mb[16] = "Hello, World!";
    memmove(mb + 7, mb, 5);
    ASSERT(strcmp(mb, "Hello, Hello!") == 0, "memmove non-overlapping");
    ASSERT(memcmp(mb + 7, "Hello", 5) == 0, "memmove non-overlapping");

    // memmove: overlapping, src before dst
    char mo[16] = "abcde";
    memmove(mo + 2, mo, 4); // "ab" "abcd" -> "ababcd" but only 5 chars written
    ASSERT(mo[2] == 'a' && mo[3] == 'b' && mo[4] == 'c' && mo[5] == 'd', "memmove overlap src<dst");

    // memmove: overlapping, dst before src
    char mo2[16] = "abcde";
    memmove(mo2, mo2 + 1, 4); // shift left
    ASSERT(mo2[0] == 'b' && mo2[1] == 'c' && mo2[2] == 'd' && mo2[3] == 'e', "memmove overlap dst<src");

    // strncpy: basic copy
    char dst[16];
    strncpy(dst, "Hello", 6);
    ASSERT(strcmp(dst, "Hello") == 0, "strncpy basic");

    // strncpy: truncation (no null terminator added)
    char dst2[4];
    strncpy(dst2, "Hello", 3);
    ASSERT(dst2[0] == 'H' && dst2[1] == 'e' && dst2[2] == 'l', "strncpy truncation");

    // strncpy: null padding when src shorter than n
    char dst3[8] = "XXXXXXX";
    strncpy(dst3, "Hi", 6);
    ASSERT(dst3[2] == '\0' && dst3[3] == '\0', "strncpy null padding");

    const char *src = "hi";
    char dest[6] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    strncpy(dest, src, 5);
    ASSERT(memcmp(dest, "hi\0\0\0f", 6) == 0, "example from cppreference");

    // strdup
    char *sd = strdup("Hello, strdup!");
    ASSERT(sd, "strdup returned NULL");
    ASSERT(strcmp(sd, "Hello, strdup!") == 0, "strdup content mismatch");
    free(sd);

    // strncmp: equal
    ASSERT(strncmp("Hello", "Hello", 5) == 0, "strncmp equal");
    // strncmp: prefix equal
    ASSERT(strncmp("Hello", "HelloWorld", 5) == 0, "strncmp prefix");
    // strncmp: less
    ASSERT(strncmp("abc", "abd", 3) < 0, "strncmp less");
    // strncmp: greater
    ASSERT(strncmp("abd", "abc", 3) > 0, "strncmp greater");
    // strncmp: n=0 always equal
    ASSERT(strncmp("abc", "xyz", 0) == 0, "strncmp n=0");

    // strrchr: last occurrence
    ASSERT(strrchr("abcabc", 'c') == strrchr("abcabc", 'c'), "strrchr consistent");
    const char *rp = strrchr("abcabc", 'a');
    ASSERT(rp && *rp == 'a' && rp[1] == 'b' && rp[2] == 'c' && rp[3] == '\0', "strrchr last 'a'");
    // strrchr: not found
    ASSERT(strrchr("abc", 'z') == NULL, "strrchr not found");
    // strrchr: last occurrence is at index 0
    ASSERT(strrchr("abc", 'a') == (void *) "abc", "strrchr at index 0");
    // strrchr: find null terminator
    const char *rend = strrchr("abc", '\0');
    ASSERT(rend && *rend == '\0', "strrchr finds null");

    // strstr: found in middle
    const char *sp = strstr("Hello, World!", "World");
    ASSERT(sp && strcmp(sp, "World!") == 0, "strstr found");
    // strstr: not found
    ASSERT(strstr("Hello", "xyz") == NULL, "strstr not found");
    // strstr: empty haystack
    ASSERT(strstr("", "abc") == NULL, "strstr empty haystack");
    // strstr: empty needle
    ASSERT(strstr("Hello", "") == (void *) "Hello", "strstr empty needle");
    // strstr: needle at start
    ASSERT(strstr("Hello", "Hell") == (void *) "Hello", "strstr at start");

    // strcasecmp: equal
    ASSERT(strcasecmp("Hello", "hello") == 0, "strcasecmp equal");
    ASSERT(strcasecmp("HELLO", "hello") == 0, "strcasecmp upper vs lower");
    // strcasecmp: less/greater
    ASSERT(strcasecmp("abc", "ABD") < 0, "strcasecmp less");
    ASSERT(strcasecmp("ABD", "abc") > 0, "strcasecmp greater");

    // strncasecmp
    ASSERT(strncasecmp("HeLLo", "hello", 5) == 0, "strncasecmp equal");
    ASSERT(strncasecmp("HeLLo!", "hello?", 5) == 0, "strncasecmp prefix");
    ASSERT(strncasecmp("abc", "ABD", 2) == 0, "strncasecmp n=2");
    ASSERT(strncasecmp("abc", "ABD", 3) < 0, "strncasecmp n=3 less");

    printf("=== All string tests passed! ===\n");

    printf("\n=== Testing snprintf ===\n");
    // memset(buf, 0, sizeof(buf));
    int r;

    // %s
    r = snprintf(buf, sizeof(buf), "%s", "Hello");
    ASSERT(strcmp(buf, "Hello") == 0, "%s content");
    ASSERT(r == 5, "%s return value");

    // %d basic
    r = snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT(strcmp(buf, "42") == 0, "%d basic");
    ASSERT(r == 2, "%d return value");

    // %d negative
    snprintf(buf, sizeof(buf), "%d", -123);
    ASSERT(strcmp(buf, "-123") == 0, "%d negative");

    // width, space-padded
    snprintf(buf, sizeof(buf), "%7d", 42);
    ASSERT(strcmp(buf, "     42") == 0, "%7d space pad");

    // zero-padded
    snprintf(buf, sizeof(buf), "%07d", 42);
    ASSERT(strcmp(buf, "0000042") == 0, "%07d zero pad");

    // zero-pad negative
    snprintf(buf, sizeof(buf), "%07d", -123);
    ASSERT(strcmp(buf, "-000123") == 0, "%07d negative");

    // precision on integer
    snprintf(buf, sizeof(buf), "%7.4d", 123);
    ASSERT(strcmp(buf, "   0123") == 0, "%7.4d");

    // zero flag ignored when precision specified
    snprintf(buf, sizeof(buf), "%07.4d", 123);
    ASSERT(strcmp(buf, "   0123") == 0, "%07.4d zero flag ignored");

    // precision + negative
    snprintf(buf, sizeof(buf), "%7.5d", -123);
    ASSERT(strcmp(buf, " -00123") == 0, "%7.5d negative");

    // %x hex
    snprintf(buf, sizeof(buf), "%x", 0xa);
    ASSERT(strcmp(buf, "a") == 0, "%x hex");
    snprintf(buf, sizeof(buf), "%x", 0);
    ASSERT(strcmp(buf, "0") == 0, "%x 0");

    // %02x zero-padded hex
    snprintf(buf, sizeof(buf), "%02x", 0xa);
    ASSERT(strcmp(buf, "0a") == 0, "%02x");
    snprintf(buf, sizeof(buf), "%.2x", 0xa);
    ASSERT(strcmp(buf, "0a") == 0, "%.2x");

    // %02x zero-padded hex with width
    snprintf(buf, sizeof(buf), "%3.2x", 0xa);
    ASSERT(strcmp(buf, " 0a") == 0, "%3.2x");

    // %s width
    snprintf(buf, sizeof(buf), "%8s", "Hi");
    ASSERT(strcmp(buf, "      Hi") == 0, "%8s width");

    // %.3s precision truncates
    snprintf(buf, sizeof(buf), "%.3s", "Hello");
    ASSERT(strcmp(buf, "Hel") == 0, "%.3s truncation");

    // %c
    snprintf(buf, sizeof(buf), "%c", 'A');
    ASSERT(strcmp(buf, "A") == 0, "%c");

    // %%
    snprintf(buf, sizeof(buf), "100%%");
    ASSERT(strcmp(buf, "100%") == 0, "%%");

    // truncation: return value is would-have-been length
    r = snprintf(buf, 4, "%d", 123456);
    ASSERT(strcmp(buf, "123") == 0, "truncation content");
    ASSERT(r == 6, "truncation return value");

    // multiple format specifiers
    snprintf(buf, sizeof(buf), "%d + %d = %d", 1, 2, 3);
    ASSERT(strcmp(buf, "1 + 2 = 3") == 0, "multiple specifiers");

    // %i same as %d
    snprintf(buf, sizeof(buf), "%i", 42);
    ASSERT(strcmp(buf, "42") == 0, "%i");

    // %.0d on 0 produces empty (only digits, padded by spaces via width)
    snprintf(buf, sizeof(buf), "%5.3d", 0);
    ASSERT(strcmp(buf, "  000") == 0, "%5.3d on 0");
    snprintf(buf, sizeof(buf), "%5.0d", 0);
    ASSERT(strcmp(buf, "     ") == 0, "%5.0d on 0");

    // . implies zero precision
    snprintf(buf, sizeof(buf), "%5.d", 0);
    ASSERT(strcmp(buf, "     ") == 0, "%5.d on 0");

    // %f default precision is 6
    snprintf(buf, sizeof(buf), "%f", 59.99);
    ASSERT(strcmp(buf, "59.990000") == 0, "%f");

    // %f if precision is 0, no decimal-point character appears
    snprintf(buf, sizeof(buf), "%.f", .1);
    ASSERT(strcmp(buf, "0") == 0, "%.f");

    // %f can take padding as well
    snprintf(buf, sizeof(buf), "%020f", 59.99);
    ASSERT(strcmp(buf, "0000000000059.990000") == 0, "%020f");

    // %f for negative
    snprintf(buf, sizeof(buf), "%020f", -59.99);
    ASSERT(strcmp(buf, "-000000000059.990000") == 0, "%020f on negative");
    snprintf(buf, sizeof(buf), "%20f", -59.99);
    ASSERT(strcmp(buf, "          -59.990000") == 0, "%20f on negative");

    // %f no period (0 precision)
    snprintf(buf, sizeof(buf), "%.f", -.1);
    ASSERT(strcmp(buf, "-0") == 0, "%.f");

    printf("=== All snprintf tests passed! ===\n");

    printf("\n=== Testing sscanf ===\n");
    int si;
    unsigned su;

    // %d: basic positive
    ASSERT(sscanf("42", "%d", &si) == 1, "sscanf %d assigned");
    ASSERT(si == 42, "sscanf %d value");

    // %d: negative
    ASSERT(sscanf("-123", "%d", &si) == 1, "sscanf %d negative assigned");
    ASSERT(si == -123, "sscanf %d negative value");

    // %d: zero
    ASSERT(sscanf("0", "%d", &si) == 1, "sscanf %d zero assigned");
    ASSERT(si == 0, "sscanf %d zero value");

    // %i: decimal (same as %d)
    ASSERT(sscanf("99", "%i", &si) == 1, "sscanf %i decimal assigned");
    ASSERT(si == 99, "sscanf %i decimal value");

    // %i: hex via 0x prefix
    ASSERT(sscanf("0xff", "%i", &si) == 1, "sscanf %i hex assigned");
    ASSERT(si == 255, "sscanf %i hex value");

    // %i: octal via leading 0
    ASSERT(sscanf("010", "%i", &si) == 1, "sscanf %i octal assigned");
    ASSERT(si == 8, "sscanf %i octal value");

    // %d: zero (not the octal prefix)
    ASSERT(sscanf("0", "%i", &si) == 1, "sscanf %i zero assigned");
    ASSERT(si == 0, "sscanf %i zero value");

    // %x: lowercase hex digits
    ASSERT(sscanf("ff", "%x", &su) == 1, "sscanf %x assigned");
    ASSERT(su == 255, "sscanf %x value");

    // %x: uppercase hex digits
    ASSERT(sscanf("FF", "%x", &su) == 1, "sscanf %x uppercase assigned");
    ASSERT(su == 255, "sscanf %x uppercase value");

    // %x: mixed case with 0x prefix
    ASSERT(sscanf("0x1A", "%x", &su) == 1, "sscanf %x prefix assigned");
    ASSERT(su == 26, "sscanf %x prefix value");

    // %x: zero
    ASSERT(sscanf("0", "%x", &su) == 1, "sscanf %x zero assigned");
    ASSERT(su == 0, "sscanf %x zero value");

    // %o: basic octal
    ASSERT(sscanf("17", "%o", &su) == 1, "sscanf %o assigned");
    ASSERT(su == 15, "sscanf %o value");

    // %o: leading zero (still octal, not auto-detect)
    ASSERT(sscanf("010", "%o", &su) == 1, "sscanf %o leading zero assigned");
    ASSERT(su == 8, "sscanf %o leading zero value");

    // %o: zero
    ASSERT(sscanf("0", "%o", &su) == 1, "sscanf %o zero assigned");
    ASSERT(su == 0, "sscanf %o zero value");

    // multiple conversions: return value is count of assigned items
    int sa, sb, sc;
    ASSERT(sscanf("1 2 3", "%d %d %d", &sa, &sb, &sc) == 3, "sscanf three ints assigned");
    ASSERT(sa == 1 && sb == 2 && sc == 3, "sscanf three ints values");

    // literal text in format
    ASSERT(sscanf("x=42", "x=%d", &si) == 1, "sscanf literal text assigned");
    ASSERT(si == 42, "sscanf literal text value");

    // failed match returns 0
    ASSERT(sscanf("abc", "%d", &si) == 0, "sscanf failed match returns 0");

    // partial match: returns count of successfully assigned before failure
    ASSERT(sscanf("42 abc", "%d %d", &sa, &sb) == 1, "sscanf partial match");
    ASSERT(sa == 42, "sscanf partial match value");

    printf("=== All sscanf tests passed! ===\n");

    printf("\n=== Testing ctype/math ===\n");

    // isprint: printable chars (0x20..0x7e)
    ASSERT(isprint(' '), "isprint space");
    ASSERT(isprint('A'), "isprint letter");
    ASSERT(isprint('~'), "isprint tilde");
    ASSERT(!isprint('\0'), "isprint NUL");
    ASSERT(!isprint('\n'), "isprint newline");
    ASSERT(!isprint('\x7f'), "isprint DEL");

    // isspace: ' ' \t \n \r \f \v
    ASSERT(isspace(' '), "isspace space");
    ASSERT(isspace('\t'), "isspace tab");
    ASSERT(isspace('\n'), "isspace newline");
    ASSERT(isspace('\r'), "isspace CR");
    ASSERT(isspace('\f'), "isspace FF");
    ASSERT(isspace('\v'), "isspace VT");
    ASSERT(!isspace('A'), "isspace letter");
    ASSERT(!isspace('0'), "isspace digit");

    // toupper
    ASSERT(toupper('a') == 'A', "toupper lowercase");
    ASSERT(toupper('z') == 'Z', "toupper lowercase z");
    ASSERT(toupper('A') == 'A', "toupper already upper");
    ASSERT(toupper('0') == '0', "toupper digit unchanged");
    ASSERT(toupper(' ') == ' ', "toupper space unchanged");

    // fabs
    ASSERT(fabs(1.5) == 1.5, "fabs positive");
    ASSERT(fabs(-1.5) == 1.5, "fabs negative");
    ASSERT(fabs(0.0) == 0.0, "fabs zero");

    printf("=== All ctype/math tests passed! ===\n");

    return 0;
}
