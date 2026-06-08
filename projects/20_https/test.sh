#!/bin/bash
# HTTPS GET via the reusable std/tls.oh library, against openssl s_server -www (Ed25519).
set -u; cd "$(dirname "$0")/../.."
ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohhttps; D=/tmp/ohtlscert
command -v openssl >/dev/null || { echo "SKIP: no openssl"; exit 0; }
mkdir -p $D
[ -f $D/cert.pem ] || openssl req -x509 -newkey ed25519 -keyout $D/key.pem -out $D/cert.pem -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
SRC="std/core.oh std/net.oh std/x25519.oh std/sha256.oh std/sha512.oh std/hkdf.oh std/tls13.oh std/aes.oh std/gcm.oh std/ed25519.oh std/tls.oh projects/20_https/https.oh"
if [ "$ARCH" = arm64 ]; then
  ./compiler/oh $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  RUN="qemu-aarch64-static $BIN.bin"
else
  ./compiler/oh $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  RUN="$BIN.bin"
fi
kill -9 $(pgrep -x openssl) 2>/dev/null; sleep 0.2
setsid openssl s_server -accept 4433 -tls1_3 -cert $D/cert.pem -key $D/key.pem -www -quiet >/dev/null 2>&1 &
sleep 0.7
out=$($RUN 2>&1); kill -9 $(pgrep -x openssl) 2>/dev/null
echo "$out"|grep -q "handshake ok" && echo "$out"|grep -q "HTTP/1.0 200" && echo "HTTPS OK (arch=$ARCH): handshake + GET via std/tls.oh" || { echo "FAIL:"; echo "$out"|head -3; exit 1; }
