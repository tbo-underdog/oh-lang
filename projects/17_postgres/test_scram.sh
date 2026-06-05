#!/bin/bash
# Validates SCRAM-SHA-256 auth end-to-end against a password-protected PostgreSQL.
#   ./projects/17_postgres/test_scram.sh            (native x86-64)
#   ARCH=arm64 ./projects/17_postgres/test_scram.sh (freestanding aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohpgs_t; ARCH="${ARCH:-x86_64}"; CONTAINER="ohpgs-test-$$"
SRC="std/core.oh std/net.oh std/pg.oh std/sha256.oh std/b64.oh std/scram.oh projects/17_postgres/pg_scram.oh"
cleanup(){ docker rm -f "$CONTAINER" >/dev/null 2>&1; }
trap cleanup EXIT
if ! command -v docker >/dev/null 2>&1; then echo "SKIP: no docker"; exit 0; fi
# POSTGRES_PASSWORD (no trust) => postgres 16 negotiates scram-sha-256 for host auth
docker run -d --rm --name "$CONTAINER" -e POSTGRES_PASSWORD=secret -p5432:5432 postgres:16 >/dev/null 2>&1 || { echo "SKIP: docker run failed (port 5432 busy?)"; exit 0; }
for i in $(seq 1 60); do docker exec "$CONTAINER" pg_isready -q 2>/dev/null && break; sleep 0.5; done
if [ "$ARCH" = "arm64" ]; then
  $OH $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  got=$(timeout 20 qemu-aarch64-static $BIN.bin 2>/dev/null)
else
  $OH $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  got=$(timeout 20 $BIN.bin 2>/dev/null)
fi
[ "$got" = "scram-ok" ] && echo "SCRAM-SHA-256 OK (arch=$ARCH): authenticated to password-protected PostgreSQL -> $got" || { echo "FAIL: got '$got' want 'scram-ok'"; exit 1; }
