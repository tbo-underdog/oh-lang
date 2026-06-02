#include <stdint.h>
static void qswap(int32_t *a, int i, int j) { int32_t t=a[i]; a[i]=a[j]; a[j]=t; }
static int qpart(int32_t *a, int lo, int hi) {
    int32_t p=a[hi]; int i=lo-1;
    for (int j=lo; j<hi; j++) if (a[j]<=p) { i++; qswap(a,i,j); }
    qswap(a,i+1,hi); return i+1;
}
static void qsort_arr(int32_t *a, int lo, int hi) {
    if (lo<hi) { int p=qpart(a,lo,hi); qsort_arr(a,lo,p-1); qsort_arr(a,p+1,hi); }
}
int main(void) {
    int32_t src[16]={9,3,7,1,0,8,4,2,6,5,15,11,13,10,12,14};
    int32_t a[16];
    for (int r=0; r<1000000; r++) {
        for (int i=0;i<16;i++) a[i]=src[i];
        qsort_arr(a,0,15);
    }
    return a[0]==0 && a[15]==15 ? 0 : 1;
}
