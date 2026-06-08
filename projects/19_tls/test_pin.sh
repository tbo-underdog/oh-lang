#!/bin/bash
# Certificate pinning: with the real server's key pinned, the genuine cert is accepted
# and a MITM presenting a DIFFERENT (valid) cert is rejected.
set -u; cd "$(dirname "$0")/../.."
D=/tmp/ohtlspin; BIN=/tmp/ohtlspin_c
command -v openssl >/dev/null || { echo "SKIP: no openssl"; exit 0; }
mkdir -p $D
openssl req -x509 -newkey ed25519 -keyout $D/good.key -out $D/good.pem -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
openssl req -x509 -newkey ed25519 -keyout $D/evil.key -out $D/evil.pem -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
# raw 32-byte ed25519 pubkey of the GOOD cert
PIN=$(openssl x509 -in $D/good.pem -noout -pubkey | openssl pkey -pubin -outform DER 2>/dev/null | tail -c 32 | xxd -p -c 32)
# patch load_pin to enable pinning with PIN
python3 - "$PIN" <<'PY'
import sys
pin=bytes.fromhex(sys.argv[1])
s=open("projects/19_tls/tls_client.oh").read()
body=" ".join(f"p[{i}]=(1){pin[i]:#04x}" for i in range(32))+"\n\\1}"
s=s.replace("// PIN_BODY\n0}", body)
open("/tmp/tls_pin_client.oh","w").write(s)
PY
SRC="std/core.oh std/net.oh std/x25519.oh std/sha256.oh std/sha512.oh std/hkdf.oh std/tls13.oh std/aes.oh std/gcm.oh std/ed25519.oh /tmp/tls_pin_client.oh"
./compiler/oh $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
run_against() { kill -9 $(pgrep -x openssl) 2>/dev/null; sleep 0.2
  setsid openssl s_server -accept 4433 -tls1_3 -cert "$1" -key "$2" -www -quiet >/dev/null 2>&1 &
  sleep 0.7; $BIN.bin 2>&1; local e=$?; kill -9 $(pgrep -x openssl) 2>/dev/null; return $e; }
echo "--- genuine cert (pin should match) ---"
g=$(run_against $D/good.pem $D/good.key); echo "$g" | grep -q "pin matched" && echo "$g"|grep -q "HTTP/1.0 200" && A=ok || A=FAIL
echo "--- MITM with different cert (pin should reject) ---"
m=$(run_against $D/evil.pem $D/evil.key); echo "$m" | grep -q "PIN MISMATCH" && B=ok || B=FAIL
echo "genuine=$A  mitm-rejected=$B"
[ "$A" = ok ] && [ "$B" = ok ] && echo "PINNING OK: genuine accepted, MITM rejected" || { echo FAIL; exit 1; }
