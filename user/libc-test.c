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

    return 0;
}
