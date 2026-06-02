#include <stdio.h>
#include <stdint.h>

static int32_t fib(int32_t n) {
    if (n <= 1) return n;
    return fib(n-1) + fib(n-2);
}

int main(void) {
    volatile int32_t r = 0;
    for (int i = 0; i < 10000; i++) {
        r = fib(30);
    }
    return (int)(r & 0xFF);
}
