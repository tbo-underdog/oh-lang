#include <stdio.h>
#include <stdint.h>

static int32_t add(int32_t a, int32_t b) { return a + b; }

int main(void) {
    volatile int32_t r = 0;
    for (int i = 0; i < 1000000000; i++) {
        r = add(3, 4);
    }
    return (int)r;
}
