#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (;;) {
    prompt:
        printf("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                printf("command line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (!strcmp(cmdline, "hello"))
            printf("Hello world from shell!\n");
        else if (!strcmp(cmdline, "readfile")) {
            // TODO readint, maybe scanf?
            printf("file descriptor> ");
            auto ch = getchar();
            putchar(ch);
            getchar(); // \n
            putchar('\n');
            char file[128];
            readfile(ch - '0', file, sizeof(file));
            printf("%s\n", file);
        } else if (!strcmp(cmdline, "writefile")) {
            // TODO readint, maybe scanf?
            printf("file descriptor> ");
            auto ch = getchar();
            putchar(ch);
            getchar(); // \n
            putchar('\n');
            char data[] = "爱宝宝\n";
            writefile(ch - '0', data, sizeof(data));
        } else if (!strcmp(cmdline, "fault")) {
            auto p = reinterpret_cast<int *>(1234);
            *p = 5678;
        } else if (strcmp(cmdline, "exit") == 0 || strcmp(cmdline, "q") == 0)
            exit(0);
        else
            printf("unknown command: %s\n", cmdline);
    }
}
