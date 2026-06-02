#include <stdint.h>
static int32_t flipbits(int32_t x) { return ~x; }
static int popcount(int32_t x) {
    int c=0;
    for (int b=0; b<32; b++) { if ((x & (1<<b)) == (1<<b)) c++; }
    return c;
}
int main(void) {
    int32_t arr[8]={5,3,8,1,9,2,7,4};
    int ok=1;
    for (int r=0; r<1000000; r++)
        for (int i=0; i<8; i++)
            if (popcount(arr[i])+popcount(flipbits(arr[i])) != 32) ok=0;
    return ok ? 0 : 1;
}
