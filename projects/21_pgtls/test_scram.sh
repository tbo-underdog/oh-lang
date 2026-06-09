#!/bin/bash
# Secure Postgres over TLS WITH SCRAM-SHA-256 password auth, end to end.
#   ./projects/21_pgtls/test_scram.sh            (native)
#   ARCH=arm64 ./projects/21_pgtls/test_scram.sh (aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohpgts; D=/tmp/ohpgts_cert; C=ohpgts-test
command -v docker >/dev/null && command -v openssl >/dev/null || { echo "SKIP: need docker+openssl"; exit 0; }
mkdir -p $D
openssl req -x509 -newkey ed25519 -keyout $D/server.key -out $D/server.crt -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
cleanup(){ docker rm -f $C >/dev/null 2>&1; }
trap cleanup EXIT
docker run -d --rm --name $C -e POSTGRES_PASSWORD=secret -p5432:5432 postgres:16 >/dev/null 2>&1 || { echo "SKIP: docker run failed"; exit 0; }
for i in $(seq 1 60); do docker exec $C pg_isready -q 2>/dev/null && break; sleep 0.5; done
docker cp $D/server.crt $C:/var/lib/postgresql/data/server.crt
docker cp $D/server.key $C:/var/lib/postgresql/data/server.key
docker exec -u root $C chown postgres:postgres /var/lib/postgresql/data/server.key /var/lib/postgresql/data/server.crt
docker exec -u root $C chmod 600 /var/lib/postgresql/data/server.key
docker exec -u postgres $C psql -U postgres -c "ALTER SYSTEM SET ssl='on'" >/dev/null 2>&1
docker restart $C >/dev/null 2>&1
for i in $(seq 1 60); do docker exec $C pg_isready -q 2>/dev/null && break; sleep 0.5; done
SRC="std/core.oh std/net.oh std/x25519.oh std/p256.oh std/sha256.oh std/sha512.oh std/hkdf.oh std/tls13.oh std/aes.oh std/gcm.oh std/ed25519.oh std/tls.oh std/pg.oh std/scram.oh std/b64.oh projects/21_pgtls/pgtls_scram.oh"
if [ "$ARCH" = arm64 ]; then
  ./compiler/oh $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  out=$(timeout 30 qemu-aarch64-static $BIN.bin 2>&1)
else
  ./compiler/oh $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  out=$(timeout 30 $BIN.bin 2>&1)
fi
echo "$out"|grep -q "SCRAM authenticated over TLS" && echo "$out"|grep -q "scram-tls-ok" \
  && echo "PG SCRAM-over-TLS OK (arch=$ARCH): TLS + SCRAM-SHA-256 + query, encrypted" || { echo "FAIL:"; echo "$out"|head -4; exit 1; }
