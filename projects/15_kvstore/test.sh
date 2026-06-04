#!/bin/bash
# ohkv end-to-end test: pipelined commands, cross-connection persistence,
# every verb, oversized-input clamping, unknown verb. Asserts exact replies.
#   ./projects/15_kvstore/test.sh            (native x86-64)
#   ARCH=arm64 ./projects/15_kvstore/test.sh (freestanding aarch64 under qemu)
set -u
cd "$(dirname "$0")/../.."
OH=./compiler/oh; BIN=/tmp/ohkv; ARCH="${ARCH:-x86_64}"
if [ "$ARCH" = "arm64" ]; then
  $OH --target aarch64-linux --emit-ir std/core.oh std/mem.oh projects/15_kvstore/kv.oh -o $BIN >/dev/null 2>&1
  clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll $BIN.ll -o $BIN.bin >/dev/null 2>&1 || { echo "link fail"; exit 1; }
  qemu-aarch64-static $BIN.bin & SRV=$!
else
  $OH std/core.oh std/mem.oh projects/15_kvstore/kv.oh -o $BIN.bin >/dev/null 2>&1 || { echo "build fail"; exit 1; }
  $BIN.bin & SRV=$!
fi
sleep 0.5
fail=0
sess(){ printf '%b' "$1" | timeout 4 nc -q1 localhost 8092 2>/dev/null | tr '\n' ' ' | sed 's/ $//'; }
chk(){ got=$(sess "$1"); if [ "$got" = "$2" ]; then echo "  PASS  $3"; else echo "  FAIL  $3: got '$got' want '$2'"; fail=1; fi; }

# pipelined session exercising every verb
chk 'S a 1\nS b 2\nG a\nG b\nE a\nE z\nK\nI ctr\nI ctr\nD a\nG a\nK\n' \
    '+ + 1 2 1 0 2 1 2 + _ 2' "pipelined: set/get/exists/count/incr/del"
# cross-connection persistence: write in one conn, read in the next
sess 'S persisted yes\n' >/dev/null
chk 'G persisted\n' 'yes' "value persists across connections"
# overwrite
chk 'S k v1\nS k v2\nG k\n' '+ + v2' "overwrite"
# oversized key (40>32) and value clamp — must not crash, must be consistent
BK=$(printf 'k%.0s' $(seq 1 40))
chk "S $BK hi\nG $BK\n" '+ hi' "oversized key clamped consistently"
# unknown verb + flush
chk 'Z x\nF\nK\n' '? + 0' "unknown verb -> ?, flush -> +, empty count 0"
# still alive after flush
chk 'S after flush_ok\nG after\n' '+ flush_ok' "alive after flush"

# edge cases
chk 'S empty \nG empty\n' '+ ' "empty value"
chk 'D ghost\n' '+' "delete missing key is a no-op +"
chk 'S cnt 41\nI cnt\nG cnt\n' '+ 42 42' "incr existing numeric"
chk 'I fresh\nI fresh\nI fresh\n' '1 2 3' "incr creates + counts up"
chk 'S spaced hello there world\nG spaced\n' '+ hello there world' "value may contain spaces"

# large value (2 KB) round-trips intact (bigger than the old 96-byte cap)
BV=$(printf 'x%.0s' $(seq 1 2000))
gv=$(printf 'S blob %s\nG blob\n' "$BV" | timeout 6 nc -q1 localhost 8092 2>/dev/null | sed -n '2p')
if [ ${#gv} -eq 2000 ]; then echo "  PASS  2 KB value round-trip"; else echo "  FAIL  2 KB value: got ${#gv} bytes"; fail=1; fi
# FIFO eviction: fill past the 3000-key cap, oldest must be evicted
ev=$({ for i in $(seq 1 3005); do printf 'S k%d v%d\n' $i $i; done; printf 'K\nG k1\nG k3005\n'; } | timeout 60 nc -q1 localhost 8092 2>/dev/null | tail -3 | tr '\n' ' ' | sed 's/ $//')
if [ "$ev" = "3000 _ v3005" ]; then echo "  PASS  eviction caps at 3000, oldest evicted"; else echo "  FAIL  eviction: '$ev' (want '3000 _ v3005')"; fail=1; fi

kill $SRV 2>/dev/null
[ $fail -eq 0 ] && echo "OHKV OK (arch=$ARCH)" || { echo "OHKV FAILED"; exit 1; }
