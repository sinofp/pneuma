#include <stdio.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (int i = 0; i < 10; ++i) {
        // for (;;) {
        for (int j = 0; j < 10; ++j)
            putchar('A');
        sleep_ms(100);
    }

    static constexpr int MSG_SIZE = 9000;
    static char buf[MSG_SIZE];
    for (int i = 0; i < MSG_SIZE; ++i)
        buf[i] = i % 256;

    int_pid result = ssend(buf, MSG_SIZE, 3);
    printf("proc-a: ssend result %ld\n", result);

    sleep_ms(500);
    bool reply = false;
    int_pid reply_from = recv(&reply, 1, 3);
    printf("proc-a: reply from pid %ld: %s\n", reply_from, reply? "OK": "Not OK");
}
