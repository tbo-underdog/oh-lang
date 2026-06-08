#!/bin/bash
# Multicore prefork echo: connections must spread across >1 worker process.
#   ./projects/18_server/test_mc.sh            (native)
#   ARCH=arm64 ./projects/18_server/test_mc.sh (aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohmcecho; PORT=7075
sed "s/,7000,/,$PORT,/" projects/18_server/echo_mc.oh > /tmp/echo_mc_t.oh
SRC="std/core.oh std/mem.oh std/net.oh std/co.oh /tmp/echo_mc_t.oh"
if [ "$ARCH" = arm64 ]; then
  $OH $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.8
out=$(python3 - "$PORT" <<'PY'
import socket,threading,collections,sys
P=int(sys.argv[1]); pids=collections.Counter(); ok=0; lock=threading.Lock()
def probe(i):
    global ok
    try:
        s=socket.create_connection(("127.0.0.1",P),timeout=5)
        s.sendall(b"pid\n"); r=b""
        while not r.endswith(b"\n"): r+=s.recv(32)
        with lock: pids[r.strip()]+=1; ok+=1
        s.close()
    except Exception: pass
ts=[threading.Thread(target=probe,args=(i,)) for i in range(200)]
[t.start() for t in ts]; [t.join() for t in ts]
print(f"{ok} {len(pids)}")
PY
)
kill $SRV 2>/dev/null; pkill -f "$BIN" 2>/dev/null
served=$(echo $out|cut -d' ' -f1); workers=$(echo $out|cut -d' ' -f2)
{ [ "$served" = 200 ] && [ "$workers" -ge 2 ]; } && echo "MULTICORE OK (arch=$ARCH): 200/200 served across $workers workers" || { echo "FAIL: served=$served workers=$workers"; exit 1; }
