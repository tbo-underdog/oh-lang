# ohkv — a key/value store in Oh

An in-memory KV store over TCP, written in Oh with raw syscalls and no libc.
Open-addressing hash table, persistent connections, and **input clamping so a
client cannot crash or overflow it**. The wire protocol is AI-centric: single-char
verbs, one line per command — cheap for a model to emit and parse.

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
# pipeline commands on one connection:
printf 'S user alice\nG user\nI hits\nI hits\nK\n' | nc -q1 localhost 8092
# -> +  alice  1  2  2
```
Connections are **persistent** (many commands per connection, line-buffered with
partial-read handling). State lives in the server process and persists across
connections. Builds freestanding (zero-dependency static binary) and on ARM64.

## Test
```sh
./projects/15_kvstore/test.sh             # native
ARCH=arm64 ./projects/15_kvstore/test.sh  # freestanding aarch64 under qemu
```
Covers every verb pipelined, cross-connection persistence, overwrite, oversized-input
clamping, and unknown verbs — asserted on both arches.

## Internals
- **Hash table**: 8192 buckets, linear probing, tombstone deletes. `$Store` struct
  holds the arrays (keys/vals/lengths/state) on an mmap heap, passed by pointer.
- **Robust by construction**: keys are clamped to 32 bytes, values to 96, parsing is
  bounded by the bytes actually read — no buffer can be overrun by any input.
- ~5,700 keys at a safe load factor (configurable via the bucket count).

## Honest scope
A fast in-memory **cache / KV store**, not a durable database:
- **in-memory only** — no disk persistence (restart = empty),
- **tombstone deletes** — heavy delete/insert churn grows probe chains; `F` (flush)
  resets,
- keys/values truncate at the size caps above.

It's a real, robust, networked datastore in Oh — not a Redis/Mongo replacement, but
genuinely usable for caching and counters.
