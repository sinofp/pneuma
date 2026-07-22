#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/misc.h>
#include <sys/syscall.h>

namespace {

// Read a line from console into buf (max len chars), returns length
int readline(char *buf, int len) {
    int i = 0;
    for (;;) {
        char ch = getchar();
        if ('\b' == ch or 127 == ch /* DEL */) {
            if (i > 0) {
                --i;
                printf("\b \b"); // Move back, overwrite with space, move back again
            }
        } else if ('\r' == ch) {
            printf("\n");
            buf[i] = '\0';
            return i;
        } else if (i < len - 1) {
            putchar(ch);
            buf[i++] = ch;
        }
    }
}

} // namespace

extern "C" void main(void) {
    for (;;) {
        printf("> ");
        char cmdline[128];
        readline(cmdline, sizeof(cmdline));

        if (!strcmp(cmdline, "hello"))
            printf("Hello world from shell!\n");
        else if (!strcmp(cmdline, "cat")) {
            printf("filename> ");
            char filename[64];
            readline(filename, sizeof(filename));

            FILE *f = fopen(filename, "r");
            if (nullptr == f) {
                printf("failed to open %s\n", filename);
                continue;
            }

            char buf[256];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);
            printf("%s\n", buf);
        } else if (!strcmp(cmdline, "write")) {
            printf("filename> ");
            char filename[64];
            readline(filename, sizeof(filename));

            printf("data> ");
            char data[128];
            int len = readline(data, sizeof(data));

            FILE *f = fopen(filename, "w");
            if (nullptr == f) {
                printf("failed to open %s\n", filename);
                continue;
            }

            fwrite(data, 1, len, f);
            fclose(f);
            printf("wrote %d bytes to %s\n", len, filename);
        } else if (!strcmp(cmdline, "sleep"))
            sleep_ms(3'000);
        else if (!strcmp(cmdline, "reboot"))
            reboot();
        else if (!strcmp(cmdline, "fault")) {
            auto p = reinterpret_cast<int *>(1234);
            *p = 5678;
        } else if (!strcmp(cmdline, "kpanic"))
            // Unknown syscall number PANICs the kernel: exercises the panic
            // path (hart freeze, header, backtrace) end-to-end over serial.
            syscall(999, 0, 0, 0);
        else if (0 == strcmp(cmdline, "exit") or 0 == strcmp(cmdline, "q"))
            exit(0);
        else if (cmdline[0] != '\0')
            printf("unknown command: %s\n", cmdline);
    }
}
