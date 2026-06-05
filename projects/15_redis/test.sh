#!/bin/bash
# Validates the Oh Redis client end-to-end. Uses, in order of preference:
# an existing Redis on :6379, a docker redis:alpine, or the bundled RESP mock.
#   ./projects/15_redis/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/15_redis/test.sh (freestanding aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohredis_t; ARCH="${ARCH:-x86_64}"
CONTAINER="ohredis-test-$$"; MOCKPID=""
cleanup(){ [ -n "$MOCKPID" ] && kill "$MOCKPID" 2>/dev/null; docker rm -f "$CONTAINER" >/dev/null 2>&1; }
trap cleanup EXIT
if nc -z 127.0.0.1 6379 2>/dev/null; then
  SRV="existing server on :6379"
elif command -v docker >/dev/null 2>&1 && docker run -d --rm --name "$CONTAINER" -p6379:6379 redis:alpine >/dev/null 2>&1; then
  SRV="real Redis (docker)"
  for i in $(seq 1 30); do [ "$(docker exec "$CONTAINER" redis-cli ping 2>/dev/null)" = "PONG" ] && break; sleep 0.3; done
else
  python3 projects/15_redis/mock_redis.py & MOCKPID=$!; SRV="RESP mock"; sleep 0.5
fi
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/net.oh std/resp.oh projects/15_redis/redis_test.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  timeout 10 qemu-aarch64-static $BIN.bin; rc=$?
else
  $OH std/core.oh std/net.oh std/resp.oh projects/15_redis/redis_test.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  timeout 10 $BIN.bin; rc=$?
fi
[ $rc -eq 0 ] && echo "REDIS CLIENT OK (arch=$ARCH, server=$SRV): +OK/bulk/nil/int all verified" || { echo "FAIL (exit $rc) against $SRV"; exit 1; }
