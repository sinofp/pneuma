#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    printf("fread returned %d bytes: '%s'\n", (int)n, buf);
    ASSERT(n == 27, "fread should return 27 bytes");

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
    printf("fwrite returned %d bytes\n", (int)n);
    ASSERT(n == strlen(msg), "fwrite should return message length");

    ret = fclose(f);
    ASSERT(0 == ret, "fclose after write failed");

    // Verify by reading back
    f = fopen("TEST.TXT", "r");
    ASSERT(f, "fopen(TEST.TXT, r) for verify failed");

    memset(buf, 0, sizeof(buf));
    n = fread(buf, 1, 64, f);
    printf("Read back %d bytes: '%s'\n", (int)n, buf);
    ASSERT(n == strlen(msg), "fread should return message length");
    ASSERT(0 == strcmp(buf, msg), "Read content should match written content");

    fclose(f);

    printf("=== All stdio tests passed! ===\n");

    return 0;
}
