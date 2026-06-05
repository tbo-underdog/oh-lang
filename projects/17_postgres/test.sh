#!/bin/bash
# Validates the Oh PostgreSQL client end-to-end (CREATE/INSERT/SELECT -> alice).
# Prefers a docker postgres with TRUST auth; else an existing PG on :5432; else skips.
#   ./projects/17_postgres/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/17_postgres/test.sh (freestanding aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohpg_t; ARCH="${ARCH:-x86_64}"; CONTAINER="ohpg-test-$$"
cleanup(){ docker rm -f "$CONTAINER" >/dev/null 2>&1; }
trap cleanup EXIT
if command -v docker >/dev/null 2>&1 && docker run -d --rm --name "$CONTAINER" -e POSTGRES_HOST_AUTH_METHOD=trust -e POSTGRES_PASSWORD=postgres -p5432:5432 postgres:16 >/dev/null 2>&1; then
  SRV="PostgreSQL (docker, trust)"
  for i in $(seq 1 60); do docker exec "$CONTAINER" pg_isready -q 2>/dev/null && break; sleep 0.5; done
elif nc -z 127.0.0.1 5432 2>/dev/null; then
  SRV="existing PostgreSQL on :5432"
else
  echo "SKIP: no PostgreSQL on :5432 and no docker"; exit 0
fi
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/net.oh std/pg.oh projects/17_postgres/pg_client.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  got=$(timeout 15 qemu-aarch64-static $BIN.bin 2>/dev/null)
else
  $OH std/core.oh std/net.oh std/pg.oh projects/17_postgres/pg_client.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  got=$(timeout 15 $BIN.bin 2>/dev/null)
fi
[ "$got" = "alice" ] && echo "POSTGRES CLIENT OK (arch=$ARCH, server=$SRV): CREATE/INSERT/SELECT -> $got" || { echo "FAIL: got '$got' want 'alice' ($SRV)"; exit 1; }
