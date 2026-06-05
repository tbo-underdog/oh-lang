# PostgreSQL client in Oh

Talks to a real PostgreSQL server over the v3 frontend/backend wire protocol — raw
sockets, no libc. Runs a `CREATE`/`INSERT`/`SELECT` round-trip.

## Layers
- **`std/pg.oh`** — the wire protocol (BIG-ENDIAN). Build StartupMessage / Query,
  check AuthenticationOk, frame messages (read-until-ReadyForQuery), and a full row API:
  `pg_nextrow` (iterate every DataRow), `pg_ncols`, `pg_coln` (any column, NULL-aware),
  `pg_msglen`. Pure byte functions, **unit-tested** in `tests/36_pg.oh` (24 asserts incl
  a 2-row × 3-col walk with a NULL).
- **`std/scram.oh`** (+ `std/sha256.oh`, `std/b64.oh`) — **SCRAM-SHA-256** auth
  (RFC 5802/7677): SHA-256, HMAC-SHA256, PBKDF2, base64 — all from scratch, no libc.
- **`pg_client.oh`** — minimal trust-auth live client (single value).
- **`pg_rows.oh`** — full multi-row / multi-column demo (CREATE/INSERT/SELECT/WHERE/
  UPDATE/DELETE/count/NULL), output verified byte-identical to `psql`.
- **`pg_scram.oh`** — full SCRAM-SHA-256 handshake against a password-protected server,
  including ServerSignature verification, then a query.

## Run
```sh
docker run -e POSTGRES_HOST_AUTH_METHOD=trust -e POSTGRES_PASSWORD=x -p5432:5432 postgres:16
./compiler/oh std/core.oh std/net.oh std/pg.oh projects/17_postgres/pg_client.oh -o pg
./pg                                     # -> alice
```

## Test
```sh
./projects/17_postgres/test.sh             # native
ARCH=arm64 ./projects/17_postgres/test.sh  # freestanding aarch64 under qemu
```
Spins up a trust-auth `postgres:16` via docker (or uses an existing PG on `:5432`), else skips.

## SCRAM-SHA-256 auth
```sh
docker run -e POSTGRES_PASSWORD=secret -p5432:5432 postgres:16   # scram-sha-256 by default
./compiler/oh std/core.oh std/net.oh std/pg.oh std/sha256.oh std/b64.oh std/scram.oh \
  projects/17_postgres/pg_scram.oh -o pgs
./pgs                                    # -> scram-ok
```
The crypto is vector-checked offline (`tests/37_sha256`, `39_b64`, `40_scram` against
RFC 4231 / 4648 / 7677), and the handshake is checked live.

## Verification status
- **Verified against real PostgreSQL 16, on x86-64 AND ARM64 (qemu):**
  - **trust auth:** CREATE, INSERT, multi-row/multi-column SELECT (int/text/numeric),
    ORDER BY, WHERE, UPDATE, DELETE, `count(*)`, NULL columns — **every row/column/count
    byte-identical to `psql`**.
  - **SCRAM-SHA-256 auth:** full SASL handshake against a password-protected server
    (`POSTGRES_PASSWORD`, no trust), ServerSignature verified, query succeeds → `scram-ok`,
    on both arches. Wrong password is correctly rejected.

## Limits (honest)
- Simple-query protocol only (no extended protocol / prepared statements / binary
  format). Values arrive as text (postgres' simple-query default) — integers, numerics
  and NULLs all parse via `pg_coln`.
- **No TLS** (no channel binding; SASL uses `n,,` / `c=biws`). The client nonce in
  `pg_scram.oh` is fixed — randomize it per connection in production.
