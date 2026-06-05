#!/bin/bash
# Validates the Oh MongoDB client end-to-end (insert {name:alice} -> find -> alice).
# Uses an existing MongoDB on :27017, else `docker run mongo:7`. Skips if neither
# (MongoDB's BSON/OP_MSG protocol is impractical to mock).
#   ./projects/16_mongo/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/16_mongo/test.sh (freestanding aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohmongo_t; ARCH="${ARCH:-x86_64}"; CONTAINER="ohmongo-test-$$"
cleanup(){ docker rm -f "$CONTAINER" >/dev/null 2>&1; }
trap cleanup EXIT
if nc -z 127.0.0.1 27017 2>/dev/null; then
  SRV="existing MongoDB on :27017"
elif command -v docker >/dev/null 2>&1 && docker run -d --rm --name "$CONTAINER" -p27017:27017 mongo:7 >/dev/null 2>&1; then
  SRV="MongoDB (docker)"
  for i in $(seq 1 60); do docker exec "$CONTAINER" mongosh --quiet --eval 'db.runCommand({ping:1}).ok' 2>/dev/null | grep -q 1 && break; sleep 0.5; done
else
  echo "SKIP: no MongoDB on :27017 and no docker"; exit 0
fi
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/net.oh std/bson.oh projects/16_mongo/mongo.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  got=$(timeout 15 qemu-aarch64-static $BIN.bin 2>/dev/null)
else
  $OH std/core.oh std/net.oh std/bson.oh projects/16_mongo/mongo.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  got=$(timeout 15 $BIN.bin 2>/dev/null)
fi
[ "$got" = "alice" ] && echo "MONGO CLIENT OK (arch=$ARCH, server=$SRV): insert {name:alice} -> find -> $got" || { echo "FAIL: got '$got' want 'alice' ($SRV)"; exit 1; }
