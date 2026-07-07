#include <stdio.h>
#include <sys/ipc.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            putchar('A');
        sleep_ms(100);
    }

    static constexpr int MSG_SIZE = 4097; // Cross page boundary, but still fast to test
    static char buf[MSG_SIZE];
    for (int i = 0; i < MSG_SIZE; ++i)
        buf[i] = i % 256;

    int_pid pid_b = 7;
    auto result = ssend(buf, MSG_SIZE, pid_b);
    printf("proc-a: ssend result %ld\n", result);

    sleep_ms(500);
    bool reply = false;
    auto reply_from = recv(&reply, 1, pid_b);
    printf("proc-a: reply from pid %ld: %s\n", reply_from, reply ? "OK" : "Not OK");
}
