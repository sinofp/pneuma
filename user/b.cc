#include <stdio.h>
#include <sys/ipc.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (int i = 0; i < 200; ++i) {
        // for (;;) {
        putchar('B');
        sleep_ms(1);
    }

    static constexpr int MSG_SIZE = 9000;
    static char buf[MSG_SIZE];

    int_pid sender = recv(buf, MSG_SIZE, ANY_SOURCE);

    bool ok = true;
    for (int i = 0; i < MSG_SIZE; ++i)
        if (buf[i] != i % 256) {
            printf("FAIL at %d: got %d expected %d\n", i, buf[i], i % 256);
            ok = false;
            break;
        }

    printf("recv from pid %ld: %s\n", sender, ok ? "OK" : "FAIL");

    int_pid reply_result = ssend(&ok, 1, 2);
    printf("proc-b: ssend reply result %ld\n", reply_result);
}
