#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Arena allocator ----
typedef struct Arena {
    char *buf;
    size_t cap;
    size_t used;
} Arena;

static inline Arena arena_new(size_t cap) {
    Arena a;
    a.buf = (char*)malloc(cap);
    if (!a.buf) { fprintf(stderr, "OOM\n"); exit(1); }
    a.cap = cap;
    a.used = 0;
    return a;
}

static inline void *arena_alloc(Arena *a, size_t sz) {
    // align to 8 bytes
    sz = (sz + 7) & ~(size_t)7;
    if (a->used + sz > a->cap) {
        fprintf(stderr, "Arena OOM (need %zu, have %zu)\n", sz, a->cap - a->used);
        exit(1);
    }
    void *p = a->buf + a->used;
    a->used += sz;
    memset(p, 0, sz);
    return p;
}

static inline char *arena_strdup(Arena *a, const char *s, size_t len) {
    char *p = (char*)arena_alloc(a, len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

static inline void arena_free(Arena *a) {
    free(a->buf);
    a->buf = NULL;
    a->cap = a->used = 0;
}

// ---- Dynamic array ----
#define DYNARRAY(T) struct { T *data; size_t len; size_t cap; }
#define DA_PUSH(da, item, alloc) do { \
    if ((da)->len >= (da)->cap) { \
        size_t newcap = (da)->cap ? (da)->cap * 2 : 8; \
        (da)->data = realloc((da)->data, newcap * sizeof(*(da)->data)); \
        if (!(da)->data) { fprintf(stderr, "OOM\n"); exit(1); } \
        (da)->cap = newcap; \
    } \
    (da)->data[(da)->len++] = (item); \
} while(0)
#define DA_FREE(da) do { free((da)->data); (da)->data = NULL; (da)->len = (da)->cap = 0; } while(0)
