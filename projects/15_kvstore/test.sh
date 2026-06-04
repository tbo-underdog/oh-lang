#!/bin/bash
# ohkv end-to-end test: start server, drive it with nc, assert every reply.
#   ./projects/15_kvstore/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/15_kvstore/test.sh (freestanding aarch64 under qemu)
set -u
cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohkv; ARCH="${ARCH:-x86_64}"
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh projects/15_kvstore/kv.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH std/core.oh projects/15_kvstore/kv.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.5
fail=0
q(){ printf '%s\n' "$1" | timeout 2 nc -q1 localhost 8092 2>/dev/null; }
chk(){ got=$(q "$1"); if [ "$got" = "$2" ]; then echo "  PASS  '$1' -> $got"; else echo "  FAIL  '$1' -> '$got' (want '$2')"; fail=1; fi; }
chk "S foo bar"   "+"
chk "S baz qux"   "+"
chk "G foo"       "bar"
chk "G baz"       "qux"
chk "G nope"      "_"
chk "K"           "2"
chk "S foo NEW"   "+"
chk "G foo"       "NEW"
chk "D foo"       "+"
chk "G foo"       "_"
chk "K"           "1"
kill $SRV 2>/dev/null
[ $fail -eq 0 ] && echo "OHKV OK (arch=$ARCH)" || { echo "OHKV FAILED"; exit 1; }
