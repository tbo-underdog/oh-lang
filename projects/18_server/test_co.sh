#!/bin/bash
# Coroutine echo server (blocking-style handler, epoll scheduler): 50 concurrent
# connections + a multi-message round-trip on one connection (suspend/resume).
#   ./projects/18_server/test_co.sh            (native)
#   ARCH=arm64 ./projects/18_server/test_co.sh (aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohcoecho; PORT=7073
SRC="std/core.oh std/mem.oh std/net.oh std/co.oh projects/18_server/echo_co.oh"
sed "s/port:=7000/port:=$PORT/;s/,7000)/,$PORT)/" projects/18_server/echo_co.oh > /tmp/echo_co_t.oh
SRC="std/core.oh std/mem.oh std/net.oh std/co.oh /tmp/echo_co_t.oh"
if [ "$ARCH" = arm64 ]; then
  $OH $SRC --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH $SRC -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.6
ok=$(python3 - "$PORT" <<'PY'
import socket,threading,sys
P=int(sys.argv[1]); res={}
def c(i):
    try:
        s=socket.create_connection(("127.0.0.1",P),timeout=5)
        m=f"co-{i}-{'z'*(i%30)}\n".encode(); s.sendall(m); d=b""
        while len(d)<len(m):
            x=s.recv(4096)
            if not x: break
            d+=x
        res[i]=(d==m); s.close()
    except Exception: res[i]=False
ts=[threading.Thread(target=c,args=(i,)) for i in range(50)]
[t.start() for t in ts]; [t.join() for t in ts]
# multi-message on one connection
try:
    s=socket.create_connection(("127.0.0.1",P),timeout=5); multi=True
    for m in (b"a\n",b"bb\n",b"ccc\n"):
        s.sendall(m); r=b""
        while len(r)<len(m): r+=s.recv(64)
        multi = multi and (r==m)
    s.close()
except Exception: multi=False
print(sum(1 for v in res.values() if v) if multi else -1)
PY
)
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
[ "$ok" = "50" ] && echo "CO ECHO OK (arch=$ARCH): 50/50 concurrent + multi-message suspend/resume" || { echo "FAIL: $ok"; exit 1; }
