# ohkv — an in-memory cache in Oh

A networked **cache** over TCP, written in Oh with raw syscalls and no libc.
Open-addressing hash table, persistent connections, **input clamped so a client
cannot crash or overflow it**, **FIFO eviction** when full, and values up to 4 KB.
The wire protocol is AI-centric: single-char verbs, one line per command.

| command | reply | meaning |
|---|---|---|
| `S <key> <value>` | `+` | set / overwrite |
| `G <key>` | `<value>` or `_` | get (`_` = missing) |
| `D <key>` | `+` | delete |
| `E <key>` | `1` or `0` | exists |
| `I <key>` | `<n>` | increment numeric value, returns new value |
| `K` | `<n>` | number of keys |
| `F` | `+` | flush all |
| *(unknown)* | `?` | unrecognized verb |

## Run
```sh
./compiler/oh std/core.oh std/mem.oh projects/15_kvstore/kv.oh -o ohkv
./ohkv                       # listens on :8092
printf 'S job:42 {"status":"done","rows":1280}\nG job:42\nI processed\nK\n' \
  | nc -q1 localhost 8092
```
Connections are **persistent** (many commands per connection, line-buffered with
partial-read handling). State persists in the server process across connections.

## Test
```sh
./projects/15_kvstore/test.sh             # native
ARCH=arm64 ./projects/15_kvstore/test.sh  # freestanding aarch64 under qemu
```
8 checks: every verb pipelined, cross-connection persistence, overwrite, oversized
clamping, unknown verbs, **2 KB value round-trip**, and **eviction past the key cap** —
asserted on both arches.

## Built as a cache for offloading batch data
- **Values up to 4 KB**, stored in demand-paged mmap slots — a 10-byte value only
  touches one page, so the slab is not wasteful despite the fixed slot size.
- **FIFO eviction**: 4096 buckets, soft cap of 3000 keys; at capacity the oldest
  entry is evicted so the cache stays bounded and keeps accepting writes (it never
  stalls or silently drops the new value).
- **Uncrashable**: keys clamp to 32 bytes, values to 4 KB, parsing is bounded by the
  bytes read — no input can overrun a buffer.
- `I` gives atomic counters; in-memory means restart = empty, which is correct for a
  cache (recompute or re-fetch).

## Tokens — honest: this is Oh's *weak* case (no savings here)
Measured against an idiomatic C equivalent (`kv.c`, same protocol/features, libc),
behaviorally cross-checked to produce identical output: **ohkv is ~+64% tokens vs C.**
That's the opposite of Oh's compute/SIMD wins, for structural reasons:
- it **bundles its allocator + mem/string helpers** (`heap_new`/`halloc`, `itoa`,
  `slen`) that C gets **free from libc** (`malloc`, `sprintf`, uncounted),
- it does **manual byte-copy loops** where C writes `memcpy`/`memmove`,
- it uses **verbose raw syscalls** (`sys(41,…)`) where C writes `socket()`.

ohkv's value is **not** token density — it's a **9 KB freestanding zero-dependency
binary**, identical on x86-64 and ARM64, with an AI-friendly protocol. Token wins
live in compute/vectorized code, not networked systems glue; we don't claim otherwise.

## Honest limits
- **Values are newline-free and ≤ 4 KB** — the line protocol delimits on `\n`, so a
  value cannot contain a newline, and 4 KB is the per-value cap. Arbitrary binary or
  multi-KB payloads would need a length-prefixed protocol (a clean future addition).
- **FIFO, not LRU** — eviction is by insertion order, not recency. TTL/expiry would
  need a clock syscall (not yet wired for ARM); also future.
- A fast bounded cache for batch offload and counters — not a durable database.
