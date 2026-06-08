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

## Blocking-style handlers (coroutines) — `std/co.oh` + `echo_co.oh`
Instead of a hand state machine, write the handler as a plain blocking function;
`std/co.oh` (stackful coroutines + the same epoll loop) makes it non-blocking:
```
#conn_handler(c:*Conn){
buf:[4096]1=0
~1{
n:=co_read(c,&buf[0],4096)   // suspends on EAGAIN; scheduler resumes on EPOLLIN
?n<=0{co_close(c)}
co_write(c,&buf[0],n)
}}
```
`co_read`/`co_write`/`co_yield`/`co_close` suspend the coroutine on `EAGAIN`; the
scheduler (`co_run`) resumes it when the fd is ready (edge-triggered). Each connection
gets a heap `Conn` + its own stack; the coroutine's address is the epoll token.

### Measured (x86-64): coroutine vs hand state machine
- **Throughput: identical** — ~200k round-trips/sec each (syscall-bound; the context
  switch is invisible), so ≥C holds.
- **Tokens: −51%** — the blocking handler file is 153 vs 311 tokens.
Verified 50/50 concurrent + a multi-message suspend/resume round-trip, x86-64 AND
aarch64 (qemu).

### Bounded memory (connection pool)
`co_init(&s, heap, &handler, port, cap)` preallocates a pool of `cap` connection slots
(each a `Conn` + its own stack) and recycles them via a free-list on `co_close`. Memory
is bounded; there is **no per-connection leak**. When the pool is full, new connections
are dropped (backpressure). Verified: 5000 sequential connections survive (the
pre-pool build aborted at 255 = 16 MB ÷ 64 KB), a 300-connection burst past `cap=128`
serves cleanly with no crash, on x86-64 AND aarch64.

### Multicore (`SO_REUSEPORT` prefork) — `echo_mc.oh`
`prefork(n)` (in `std/net.oh`) forks `n` shared-nothing worker processes via
`clone(SIGCHLD)`; each runs its own heap + epoll + `SO_REUSEPORT` listen socket, and
the kernel load-balances accepts across them. No locks, no shared state, no atomics —
the design the grill chose. `echo_mc.oh` answers a `pid` request with the serving
worker's PID; a 200-connection probe is spread across all 4 workers (~50 each),
verified x86-64 AND aarch64 (qemu). `./test_mc.sh`.

## Next
TLS.
