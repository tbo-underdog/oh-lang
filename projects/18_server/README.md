# Concurrent epoll server in Oh

A single-threaded, edge-of-the-art event-loop server — raw syscalls, no libc, freestanding.
Handles many simultaneous connections on one thread via `epoll`.

## Why this shape
North star = concurrent network servers under {minimum tokens, ≥C performance}. The
grilled design: **single-threaded epoll loop, scaled across cores by `SO_REUSEPORT`
prefork (shared-nothing processes)** — race-free, zero synchronization, linear scaling,
and the loop is token-cheap. Per-connection state lives in an arena freed in O(1) on
close (`heap_mark`/`heap_restore`), so no general `free()` is needed.

## Primitives (`std/net.oh`)
`listen_on(port)`, `accept_nb`, `set_nonblock`, `epoll_new`, `epoll_add`, `epoll_del`,
`epoll_wait_n`, `ev_fd`/`ev_events`/`ev_set`, `now_ns`, `rand_bytes`.
`struct epoll_event` differs by arch (x86 packed 12B/data@4, arm64 16B/data@8); the
layout is derived from the `archid()` compile-time constant so one source serves both.

## Run / test
```sh
./compiler/oh std/core.oh std/net.oh projects/18_server/echo.oh -o echo && ./echo  # port 7000
./projects/18_server/test.sh             # 50 concurrent connections, native
ARCH=arm64 ./projects/18_server/test.sh  # same under qemu (aarch64, freestanding)
```

## Verified
50 simultaneous connections each echo their own payload correctly, clean shutdown, on
**x86-64 AND aarch64 (qemu)** — on a real Linux kernel, no libc.

## Next iteration
`echo.oh` is a hand-written state machine. The planned compiler **async-transform**
(stackless coroutines, whole-program-monomorphized scheduler) will let handlers be
written blocking-style and compile to this same state machine — minimum tokens *and* ≥C.
TLS and `SO_REUSEPORT` prefork are also pending.
