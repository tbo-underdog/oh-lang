static int dot(const int *restrict a, const int *restrict b, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
int main(void) {
    int a[1024], b[1024];
    for (int i = 0; i < 1024; i++) { a[i] = i & 255; b[i] = (i * 3) & 255; }
    int acc = 0;
    for (int r = 0; r < 50000; r++) {
        a[r & 1023] = r & 255;
        acc = acc + dot(a, b, 1024);
    }
    return acc & 255;
}
