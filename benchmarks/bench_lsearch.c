#include <stdint.h>
static int lsearch(const int32_t *a, int n, int val) {
    for (int i=0; i<n; i++) if (a[i]==val) return i;
    return -1;
}
int main(void) {
    int32_t a[10]={3,7,1,9,5,2,8,4,6,0};
    int r = 0;
    for (int i=0; i<10000000; i++) r += lsearch(a,10,5);
    return r == 40000000 ? 0 : 1;
}
