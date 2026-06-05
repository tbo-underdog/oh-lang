# PostgreSQL client in Oh

Talks to a real PostgreSQL server over the v3 frontend/backend wire protocol — raw
sockets, no libc. Runs a `CREATE`/`INSERT`/`SELECT` round-trip.

## Layers
- **`std/pg.oh`** — the wire protocol (BIG-ENDIAN). Build StartupMessage / Query,
  check AuthenticationOk, frame messages (read-until-ReadyForQuery), parse DataRow.
  Pure byte functions, **unit-tested** in `tests/36_pg.oh`.
- **`pg_client.oh`** — the live client: startup + simple-query protocol.

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
- **Verified against real PostgreSQL 16, on x86-64 AND ARM64 (qemu):** the client
  creates a table, inserts `'alice'`, selects it back, and `psql` confirms the row is
  actually stored. Message framing reads until `ReadyForQuery` (handles split reads),
  big-endian decode is unit-tested.

## Limits (honest)
- **TRUST auth only.** Password auth uses **SCRAM-SHA-256** (HMAC-SHA256 + PBKDF2),
  which is not implemented yet — it's the next real piece of work. Run the server with
  `POSTGRES_HOST_AUTH_METHOD=trust` (or `md5`/SCRAM support is a TODO).
- Simple-query protocol only (no extended protocol / prepared statements). Text columns.
  No TLS. These cover real CREATE/INSERT/SELECT against a trust-auth server.
