#!/bin/bash
# Proves the router distinguishes GET vs POST by reading the request.
# PASS only if GET returns "GET ok" AND POST returns "POST ok".
set -u
cd "$(dirname "$0")/../.."
BIN=/tmp/router_test
ARCH="${ARCH:-x86_64}"
if [ "$ARCH" = "arm64" ]; then
  ./compiler/oh --target aarch64-linux --emit-ir std/core.oh std/str.oh projects/12_http_router/server.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  ./compiler/oh std/core.oh std/str.oh projects/12_http_router/server.oh -o $BIN.bin >/dev/null 2>&1
  $BIN.bin & SRV=$!
fi
sleep 1.2
g=$(curl -s -X GET  http://127.0.0.1:8080/)
p=$(curl -s -X POST http://127.0.0.1:8080/ -d 'hello')
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null

fail=0
[ "$g" = "GET ok" ]  && echo "  PASS  GET  -> '$g'"  || { echo "  FAIL  GET  -> '$g' (want 'GET ok')";  fail=1; }
[ "$p" = "POST ok" ] && echo "  PASS  POST -> '$p'" || { echo "  FAIL  POST -> '$p' (want 'POST ok')"; fail=1; }
[ $fail -eq 0 ] && echo "ROUTER OK (arch=$ARCH)" || { echo "ROUTER FAILED"; exit 1; }
