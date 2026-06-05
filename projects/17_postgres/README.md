# PostgreSQL client in Oh

Talks to a real PostgreSQL server over the v3 frontend/backend wire protocol — raw
sockets, no libc. Runs a `CREATE`/`INSERT`/`SELECT` round-trip.

## Layers
- **`std/pg.oh`** — the wire protocol (BIG-ENDIAN). Build StartupMessage / Query,
  check AuthenticationOk, frame messages (read-until-ReadyForQuery), and a full row API:
  `pg_nextrow` (iterate every DataRow), `pg_ncols`, `pg_coln` (any column, NULL-aware),
  `pg_msglen`. Pure byte functions, **unit-tested** in `tests/36_pg.oh` (24 asserts incl
  a 2-row × 3-col walk with a NULL).
- **`pg_client.oh`** — minimal live client (single value).
- **`pg_rows.oh`** — full multi-row / multi-column demo (CREATE/INSERT/SELECT/WHERE/
  UPDATE/DELETE/count/NULL), output verified byte-identical to `psql`.

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

## Verification status
- **Verified against real PostgreSQL 16, on x86-64 AND ARM64 (qemu):** exercised CREATE,
  INSERT, multi-row/multi-column SELECT (int/text/numeric), ORDER BY, WHERE, UPDATE,
  DELETE, `count(*)`, and NULL columns — **every row/column/count byte-identical to
  `psql`**. Message framing reads until `ReadyForQuery` (handles split reads).

## Limits (honest)
- **TRUST auth only.** Password auth uses **SCRAM-SHA-256** (HMAC-SHA256 + PBKDF2),
  which is not implemented yet — it's the next real piece of work. Run the server with
  `POSTGRES_HOST_AUTH_METHOD=trust` (or `md5`/SCRAM support is a TODO).
- Simple-query protocol only (no extended protocol / prepared statements / binary
  format / TLS). Values arrive as text (postgres' simple-query default) — integers,
  numerics and NULLs all parse via `pg_coln`. Covers real CRUD against a trust-auth server.
