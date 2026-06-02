#include <stdio.h>
#include <stdint.h>

static int32_t sum(int32_t n) {
    int32_t s = 0;
    for (int32_t i = 1; i <= n; i++) s += i;
    return s;
}

int main(void) {
    volatile int32_t r = 0;
    for (int i = 0; i < 10000000; i++) {
        r = sum(100);
    }
    return (int)(r & 0xFF);
}
