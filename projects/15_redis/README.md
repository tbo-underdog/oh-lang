# Redis client in Oh

Talks to a real Redis server over its RESP wire protocol — raw sockets, no libc.
Demonstrates the ecosystem-integration direction: use the databases you already run.

## Layers
- **`std/resp.oh`** — the RESP protocol (encode commands, decode replies). Pure byte
  functions, **fully unit-tested** in `tests/34_resp.oh` (no server needed).
- **`redis.oh`** — the live client: `connect_to` + send + recv + decode. A SET/GET demo.

## Run (against a real Redis)
```sh
redis-server &                  # or: docker run -p6379:6379 redis
./compiler/oh std/core.oh std/net.oh std/resp.oh projects/15_redis/redis.oh -o redis
./redis                         # -> hello
```

## Test (no Redis needed)
```sh
./projects/15_redis/test.sh             # native
ARCH=arm64 ./projects/15_redis/test.sh  # freestanding aarch64 under qemu
```
Runs the client against a correct minimal RESP server (`mock_redis.py`) and asserts the
SET→GET roundtrip returns `hello`. Passes on both arches.

## Honest scope / verification
- The **RESP protocol** (`std/resp`) is unit-tested against known byte sequences.
- The **live client** is validated end-to-end against a correct RESP mock here. Against
  real Redis it speaks the identical protocol for SET/GET; we haven't run it against
  redis-the-product in this environment (none installed), so validate there before
  relying on it.
- Reads assume a reply fits one `recv` (fine for small values; production loops until a
  full RESP frame is parsed). Auth/TLS/pipelining/cluster are not implemented.

## Why Redis first
RESP is a simple text-ish protocol — the most achievable real DB client. Postgres
(binary protocol + SCRAM auth) and MongoDB (BSON + a richer wire protocol) are heavier
and are the next groundwork, reusing this same connection + protocol-module pattern.
