struct R { int w, h; };
int main(void) {
    struct R r;
    int acc = 0;
    for (int i = 0; i < 2000000; i++) {
        r.w = i % 100 + 1;
        r.h = (i * 7) % 100 + 1;
        acc += r.w * r.h;
    }
    return acc;
}
