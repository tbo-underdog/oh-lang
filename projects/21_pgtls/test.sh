#!/bin/bash
# Secure Postgres-over-TLS: SSLRequest -> TLS 1.3 (P-256 ECDHE, Ed25519 cert verify) ->
# Postgres startup + query over the encrypted channel. Needs docker + openssl.
#   ./projects/21_pgtls/test.sh            (native)
#   ARCH=arm64 ./projects/21_pgtls/test.sh (aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohpgtls; D=/tmp/ohpgtls_cert; C=ohpgtls-test
command -v docker >/dev/null && command -v openssl >/dev/null || { echo "SKIP: need docker+openssl"; exit 0; }
mkdir -p $D
openssl req -x509 -newkey ed25519 -keyout $D/server.key -out $D/server.crt -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
cleanup(){ docker rm -f $C >/dev/null 2>&1; }
trap cleanup EXIT
docker run -d --rm --name $C -e POSTGRES_HOST_AUTH_METHOD=trust -e POSTGRES_PASSWORD=x -p5432:5432 postgres:16 >/dev/null 2>&1 || { echo "SKIP: docker run failed"; exit 0; }
for i in $(seq 1 60); do docker exec $C pg_isready -q 2>/dev/null && break; sleep 0.5; done
docker cp $D/server.crt $C:/var/lib/postgresql/data/server.crt
docker cp $D/server.key $C:/var/lib/postgresql/data/server.key
docker exec -u root $C chown postgres:postgres /var/lib/postgresql/data/server.key /var/lib/postgresql/data/server.crt
docker exec -u root $C chmod 600 /var/lib/postgresql/data/server.key
docker exec -u postgres $C psql -U postgres -c "ALTER SYSTEM SET ssl='on'" >/dev/null 2>&1
docker restart $C >/dev/null 2>&1
for i in $(seq 1 60); do docker exec $C pg_isready -q 2>/dev/null && break; sleep 0.5; done
SRC="std/core.oh std/net.oh std/x25519.oh std/p256.oh std/sha256.oh std/sha512.oh std/hkdf.oh std/tls13.oh std/aes.oh std/gcm.oh std/ed25519.oh std/tls.oh std/pg.oh projects/21_pgtls/pgtls.oh"
if [ "$ARCH" = arm64 ]; then
  ./compiler/oh $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  out=$(timeout 30 qemu-aarch64-static $BIN.bin 2>&1)
else
  ./compiler/oh $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  out=$(timeout 30 $BIN.bin 2>&1)
fi
echo "$out"|grep -q "cert verified" && echo "$out"|grep -q "query over TLS -> pgtls-ok" \
  && echo "PG-over-TLS OK (arch=$ARCH): TLS+cert-verify + query over encrypted channel" || { echo "FAIL:"; echo "$out"|head -4; exit 1; }
