#include <stdio.h>
#include <stdint.h>

static int32_t maxarr(int32_t *a, int32_t n) {
    int32_t m = a[0];
    for (int32_t i = 1; i < n; i++) {
        if (a[i] > m) m = a[i];
    }
    return m;
}

int main(void) {
    int32_t arr[5] = {1, 5, 3, 2, 4};
    volatile int32_t r = 0;
    for (int i = 0; i < 1000000000; i++) {
        r = maxarr(arr, 5);
    }
    return (int)(r & 0xFF);
}
