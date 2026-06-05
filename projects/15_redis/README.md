# Redis client in Oh

Talks to a real Redis server over its RESP wire protocol — raw sockets, no libc.
Use the databases you already run, from Oh.

## Layers
- **`std/resp.oh`** — the RESP protocol (encode commands, decode replies). Pure byte
  functions, **unit-tested** in `tests/34_resp.oh` (no server needed).
- **`redis.oh`** — a live SET/GET demo. **`redis_test.oh`** — an assertion client that
  exercises every reply type (`+OK`, `$bulk`, `$-1` nil, `:int`).

## Run
```sh
redis-server &                  # or: docker run -p6379:6379 redis
./compiler/oh std/core.oh std/net.oh std/resp.oh projects/15_redis/redis.oh -o redis
./redis                         # -> hello
```

## Test
```sh
./projects/15_redis/test.sh             # native
ARCH=arm64 ./projects/15_redis/test.sh  # freestanding aarch64 under qemu
```
Uses, in order: an existing Redis on `:6379`, a `docker run redis:alpine`, or a bundled
RESP mock (`mock_redis.py`) if neither is available — so it runs anywhere, and against
real Redis when it can.

## Verification status
- **Verified against real Redis 8.x (docker), on x86-64 AND ARM64 (qemu):** the full
  `SET`/`GET`/`GET-missing`/`INCR`/`DEL` flow returns the correct replies, and
  `redis-cli` confirms the Oh client's writes actually landed in Redis.
- The RESP protocol layer is additionally unit-tested against known byte sequences.

## Limits
- Reads assume a reply fits one `recv` (fine for small values; production loops until a
  full RESP frame is parsed).
- No AUTH, TLS, pipelining, or cluster support yet. SET/GET/INCR/DEL and the four core
  reply types are covered.
