#include <stdio.h>
#include <stdint.h>

static int32_t absi(int32_t x) { return x < 0 ? -x : x; }

int main(void) {
    volatile int32_t r = 0;
    for (int i = -500000000; i < 500000000; i++) {
        r = absi(i);
    }
    return (int)(r & 0xFF);
}
