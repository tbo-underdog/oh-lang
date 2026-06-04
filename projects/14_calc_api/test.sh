#!/bin/bash
# Calculator API test: start server, curl every route, assert, stop.
#   ./projects/14_calc_api/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/14_calc_api/test.sh (freestanding aarch64 under qemu)
set -u
cd "$(dirname "$0")/../.."
OH=./compiler/oh
BIN=/tmp/calc_api
ARCH="${ARCH:-x86_64}"
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/str.oh projects/14_calc_api/api.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH std/core.oh std/str.oh projects/14_calc_api/api.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.5
fail=0
check(){ got=$(curl -s "$1"); if [ "$got" = "$2" ]; then echo "  PASS  $1 -> $got"; else echo "  FAIL  $1 -> '$got' (want '$2')"; fail=1; fi; }
check "localhost:8091/status"            "ok"
check "localhost:8091/add?a=3&b=4"       "7"
check "localhost:8091/add?a=-10&b=2"     "-8"
check "localhost:8091/divide?a=20&b=4"   "5"
check "localhost:8091/divide?a=7&b=0"    "error: division by zero"
kill $SRV 2>/dev/null
[ $fail -eq 0 ] && echo "CALC API OK (arch=$ARCH)" || { echo "CALC API FAILED"; exit 1; }
