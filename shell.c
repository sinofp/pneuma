#include "user.h"

void main(void) {
    for (;;) {
prompt:
        puts("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                puts("command line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (strcmp(cmdline, "hello") == 0)
            puts("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0 || strcmp(cmdline, "q") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
    }
}
