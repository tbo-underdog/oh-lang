static int scale(int *restrict a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        a[i] = (a[i] * 3 + 1) & 255;
        s = s + a[i];
    }
    return s;
}
int main(void) {
    int b[1024];
    for (int i = 0; i < 1024; i++) b[i] = i & 255;
    int acc = 0;
    for (int r = 0; r < 20000; r++) acc = acc + scale(b, 1024);
    return acc & 255;
}
