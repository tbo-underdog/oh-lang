#!/bin/bash
# Concurrent epoll echo server: 50 simultaneous connections must each echo correctly.
#   ./projects/18_server/test.sh            (native)
#   ARCH=arm64 ./projects/18_server/test.sh (aarch64 under qemu)
set -u; cd "$(dirname "$0")/../.."
OH=./compiler/oh; ARCH="${ARCH:-x86_64}"; BIN=/tmp/ohecho_t; PORT=7071
sed "s/port:=7000/port:=$PORT/" projects/18_server/echo.oh > /tmp/echo_t.oh
if [ "$ARCH" = arm64 ]; then
  $OH std/core.oh std/net.oh /tmp/echo_t.oh --target aarch64-linux --emit-ir -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH std/core.oh std/net.oh /tmp/echo_t.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.6
ok=$(python3 - "$PORT" <<'PY'
import socket,threading,sys
P=int(sys.argv[1]); res={}
def c(i):
    try:
        s=socket.create_connection(("127.0.0.1",P),timeout=5)
        m=f"conn-{i}-{'z'*(i%30)}\n".encode(); s.sendall(m); d=b""
        while len(d)<len(m):
            x=s.recv(4096); 
            if not x: break
            d+=x
        res[i]=(d==m); s.close()
    except Exception as e: res[i]=False
ts=[threading.Thread(target=c,args=(i,)) for i in range(50)]
[t.start() for t in ts]; [t.join() for t in ts]
print(sum(1 for v in res.values() if v))
PY
)
python3 -c "import socket; s=socket.create_connection(('127.0.0.1',$PORT)); s.sendall(b'quit\n'); s.recv(64); s.close()" 2>/dev/null
wait $SRV 2>/dev/null
[ "$ok" = "50" ] && echo "ECHO SERVER OK (arch=$ARCH): 50/50 concurrent connections echoed" || { echo "FAIL: $ok/50"; exit 1; }
