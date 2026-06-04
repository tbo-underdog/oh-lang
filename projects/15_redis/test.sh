#!/bin/bash
# Validates the Oh Redis client against a RESP mock (or a real Redis if one is up).
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohredis; ARCH="${ARCH:-x86_64}"
python3 projects/15_redis/mock_redis.py & MOCK=$!; sleep 0.5
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/net.oh std/resp.oh projects/15_redis/redis.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; kill $MOCK; exit 1; }
  got=$(timeout 5 qemu-aarch64-static $BIN.bin 2>/dev/null)
else
  $OH std/core.oh std/net.oh std/resp.oh projects/15_redis/redis.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; kill $MOCK; exit 1; }
  got=$(timeout 5 $BIN.bin 2>/dev/null)
fi
kill $MOCK 2>/dev/null
if [ "$got" = "hello" ]; then echo "REDIS CLIENT OK (arch=$ARCH): SET greeting hello; GET -> $got"; else echo "FAIL: got '$got' want 'hello'"; exit 1; fi
