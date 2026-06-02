#!/bin/bash
# Oh test runner — the GREEN check.
#
# TDD workflow: each test declares an expected exit code. A program PASSES only
# if it compiles AND its process exit code matches. This is the single source of
# truth for "true pass / true fail" — humans and AI agree on this number.
#
# Usage:
#   ./run_tests.sh            # run on the native host (x86-64)
#   ARCH=arm64 ./run_tests.sh # cross-compile + run under qemu-aarch64-static
set -u
cd "$(dirname "$0")"
OH=./compiler/oh
ARCH="${ARCH:-x86_64}"
pass=0; fail=0; failed=()

make -C compiler -s 2>/dev/null || { echo "compiler build FAILED"; exit 1; }

run() { # name  expected_exit  inputs...
  local name="$1" want="$2"; shift 2
  local ll=/tmp/oht_$name
  if [ "$ARCH" = "arm64" ]; then
    $OH --target aarch64-linux --emit-ir "$@" -o "$ll" >/dev/null 2>&1
    clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld \
        tooling/start_arm64.s "$ll.ll" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[link]"); return; }
    timeout 30 qemu-aarch64-static "$ll.bin" >/dev/null 2>&1; got=$?
  else
    $OH "$@" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[compile]"); return; }
    timeout 30 "$ll.bin" >/dev/null 2>&1; got=$?
  fi
  if [ "$got" -eq "$want" ]; then pass=$((pass+1)); printf "  PASS  %-16s (exit %s)\n" "$name" "$got"
  else fail=$((fail+1)); failed+=("$name"); printf "  FAIL  %-16s (got %s, want %s)\n" "$name" "$got" "$want"; fi
}

echo "== core tests =="
run 01_add        7  tests/01_add.oh
run 02_abs        5  tests/02_abs.oh
run 03_sum        55 tests/03_sum.oh
run 04_fibonacci  55 tests/04_fibonacci.oh
run 05_max_array  5  tests/05_max_array.oh

echo "== projects (exit 0 = all internal assertions pass) =="
run 01_hashmap        0 projects/01_hashmap/hashmap.oh
run 02_arena          0 projects/02_arena_allocator/arena.oh
run 03_sorting        0 projects/03_sorting/sorting.oh
run 04_stack          0 projects/04_stack_ds/stack.oh
run 05_bitset         0 projects/05_bitset/bitset.oh
run 06_dijkstra       0 projects/06_dijkstra/dijkstra.oh
run 07_quicksort      0 projects/07_quicksort/quicksort.oh
run 08_linear_search  0 projects/08_linear_search/lsearch.oh
run 09_bitflip        0 projects/09_bitflip/bitflip.oh

echo "== stdlib =="
run stdlib  0 std/core.oh std/str.oh std/io.oh tests/stdlib/test_std.oh

echo "-------------------------------------------"
echo "RESULT: $pass passed, $fail failed  (arch=$ARCH)"
[ $fail -eq 0 ] || { echo "FAILED: ${failed[*]}"; exit 1; }
echo "ALL GREEN"
