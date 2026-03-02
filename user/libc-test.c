#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("ASSERT FAILED: [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        return 1; \
    } \
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
    ASSERT(!strcmp("Hello from FAT filesystem!\n", buf), "fread HELLO.TXT should return 'Hello from FAT filesystem!\\n'");

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
    char *uninit = (char *)malloc(5);
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
    memmove(mo + 2, mo, 4);  // "ab" "abcd" -> "ababcd" but only 5 chars written
    ASSERT(mo[2] == 'a' && mo[3] == 'b' && mo[4] == 'c' && mo[5] == 'd', "memmove overlap src<dst");

    // memmove: overlapping, dst before src
    char mo2[16] = "abcde";
    memmove(mo2, mo2 + 1, 4);  // shift left
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

    const char* src = "hi";
    char dest[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
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
    ASSERT(strrchr("abc", 'a') == (void *)"abc", "strrchr at index 0");
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
    ASSERT(strstr("Hello", "") == (void *)"Hello", "strstr empty needle");
    // strstr: needle at start
    ASSERT(strstr("Hello", "Hell") == (void *)"Hello", "strstr at start");

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

    return 0;
}
