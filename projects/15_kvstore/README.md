# ohkv — a tiny key/value store in Oh

A minimal in-memory KV store over TCP, written in Oh with raw syscalls and no libc.
The wire protocol is deliberately **AI-centric**: single-character verbs, one line
per command — cheap for a model to emit and parse.

| command | reply | meaning |
|---|---|---|
| `S <key> <value>` | `+` | set / overwrite |
| `G <key>` | `<value>` or `_` | get (`_` = missing) |
| `D <key>` | `+` | delete |
| `K` | `<n>` | number of keys |

## Run
```sh
./compiler/oh std/core.oh projects/15_kvstore/kv.oh -o ohkv
./ohkv                       # listens on :8092
# in another shell:
printf 'S user alice\n' | nc -q1 localhost 8092   # +
printf 'G user\n'       | nc -q1 localhost 8092   # alice
```
State persists in the server process across connections. Builds freestanding
(zero-dependency static binary) and on ARM64 the same way as the other projects.

## Test
```sh
./projects/15_kvstore/test.sh             # native
ARCH=arm64 ./projects/15_kvstore/test.sh  # freestanding aarch64 under qemu
```

## Honest scope
This is a **cache / KV store, not a database.** It is:
- **in-memory only** — no disk persistence (restart = empty),
- **bounded** — 256 keys, 32-byte keys, 64-byte values (fixed at compile time),
- **linear-scan** — O(n) lookup (fine for small N; a hash index is the next step),
- **single command per connection.**

It demonstrates a real networked datastore in Oh — structs (`$Store`) for state,
a heap-free fixed arena, pointer-to-struct passing, and raw-socket I/O — end-to-end
tested on both arches. It is **not** a Redis/Mongo replacement.

## On SQL / real DB clients
A Redis (RESP) or Postgres client is straightforward to *write* from the protocol
spec, but this repo only ships what it can **verify**, and no Redis/Postgres server
was available to test against. So those clients are intentionally not included yet
rather than shipped unvalidated — consistent with the project's "prove it" rule.
