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
        tooling/start_arm64.s tooling/rt.ll "$ll.ll" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[link]"); return; }
    timeout 30 qemu-aarch64-static "$ll.bin" >/dev/null 2>&1; got=$?
  else
    $OH "$@" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[compile]"); return; }
    timeout 30 "$ll.bin" >/dev/null 2>&1; got=$?
  fi
  if [ "$got" -eq "$want" ]; then pass=$((pass+1)); printf "  PASS  %-16s (exit %s)\n" "$name" "$got"
  else fail=$((fail+1)); failed+=("$name"); printf "  FAIL  %-16s (got %s, want %s)\n" "$name" "$got" "$want"; fi
}

# Freestanding self-containment check: build -nostdlib with the start stub + rt
# (memset/memcpy/memmove/memcmp), confirm ZERO dynamic deps, and run. This guards
# the "always self-contained" guarantee even when clang synthesizes mem* libcalls.
run_fs() { # name  expected_exit  inputs...
  local name="$1" want="$2"; shift 2
  local ll=/tmp/ohfs_$name
  if [ "$ARCH" = "arm64" ]; then
    $OH --target aarch64-linux --emit-ir "$@" -o "$ll" >/dev/null 2>&1
    clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld \
        tooling/start_arm64.s tooling/rt.ll "$ll.ll" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[fs-link]"); return; }
    timeout 30 qemu-aarch64-static "$ll.bin" >/dev/null 2>&1; got=$?
  else
    $OH --emit-ir "$@" -o "$ll" >/dev/null 2>&1
    clang -O3 -nostdlib -static tooling/start_x86_64.s tooling/rt.ll "$ll.ll" -o "$ll.bin" >/dev/null 2>&1 || { fail=$((fail+1)); failed+=("$name[fs-link]"); return; }
    # must have NO dynamic dependencies
    if file "$ll.bin" 2>/dev/null | grep -q "dynamically linked"; then fail=$((fail+1)); failed+=("$name[has-deps]"); return; fi
    timeout 30 "$ll.bin" >/dev/null 2>&1; got=$?
  fi
  if [ "$got" -eq "$want" ]; then pass=$((pass+1)); printf "  PASS  %-16s (exit %s, freestanding)\n" "$name" "$got"
  else fail=$((fail+1)); failed+=("$name"); printf "  FAIL  %-16s (got %s, want %s)\n" "$name" "$got" "$want"; fi
}

echo "== core tests =="
run 01_add        7  tests/01_add.oh
run 02_abs        5  tests/02_abs.oh
run 03_sum        55 tests/03_sum.oh
run 04_fibonacci  55 tests/04_fibonacci.oh
run 05_max_array  5  tests/05_max_array.oh
run 06_tailrec    128 tests/06_tailrec.oh
run 07_compound   0  tests/07_compound.oh
run 08_arrayfill  0  tests/08_arrayfill.oh
run 09_strtype    0  tests/09_strtype.oh
run 10_struct     0  tests/10_struct.oh
run 11_simd       0  tests/11_simd.oh
run 12_ptrstruct  0  tests/12_ptrstruct.oh
run 13_mem_safety 0  std/mem.oh tests/13_mem_safety.oh
run 14_oom        137 std/mem.oh tests/14_oom.oh
run 16_pointers   0  tests/16_pointers.oh
run 17_ternary    0  tests/17_ternary.oh
run 18_arith      0  tests/18_arith.oh
run 19_alloc      0  std/mem.oh tests/19_alloc.oh
run 20_stdlib     0  std/core.oh std/mem.oh std/map.oh std/buf.oh std/math.oh tests/20_stdlib.oh
run 21_vecfull    137 std/mem.oh std/vec.oh tests/21_vecfull.oh
run 22_mapfull    137 std/mem.oh std/map.oh tests/22_mapfull.oh
run 23_float      0  tests/23_float.oh
run 24_str        0  std/core.oh std/str.oh tests/24_str.oh
run 25_shortcirc  0  tests/25_shortcircuit.oh
run 26_loops      0  tests/26_loops.oh
run 27_inttypes   0  tests/27_inttypes.oh
run 28_corelib    0  std/core.oh tests/28_corelib.oh
run 29_recursion  0  tests/29_recursion.oh
run 30_builtins   0  tests/30_builtins.oh
run 31_bool       0  tests/31_bool.oh
run 32_json       0  std/core.oh std/str.oh std/json.oh tests/32_json.oh
run 33_life       0  projects/13_life/life_core.oh tests/33_life.oh
run 34_resp       0  std/core.oh std/resp.oh tests/34_resp.oh
run 35_bson       0  std/core.oh std/bson.oh tests/35_bson.oh
run 36_pg         0  std/core.oh std/pg.oh tests/36_pg.oh
run 37_sha256     0  std/core.oh std/sha256.oh tests/37_sha256.oh
run 38_hexlit     0  tests/38_hexlit.oh
run 39_b64        0  std/core.oh std/b64.oh tests/39_b64.oh
run 40_scram      0  std/core.oh std/sha256.oh std/b64.oh std/scram.oh tests/40_scram.oh
run 41_loop       0  std/core.oh std/net.oh tests/41_loop.oh
run 42_coroutine  0  std/core.oh tests/42_coroutine.oh
run 43_hkdf       0  std/core.oh std/sha256.oh std/hkdf.oh tests/43_hkdf.oh
run 44_aes        0  std/core.oh std/aes.oh tests/44_aes.oh
run 45_gcm        0  std/core.oh std/aes.oh std/gcm.oh tests/45_gcm.oh
run 46_x25519     0  std/core.oh std/x25519.oh tests/46_x25519.oh
run 47_tls13      0  std/core.oh std/sha256.oh std/hkdf.oh std/tls13.oh tests/47_tls13.oh
run 48_sha512     0  std/core.oh std/sha512.oh tests/48_sha512.oh
run 49_ed25519    0  std/core.oh std/x25519.oh std/sha512.oh std/ed25519.oh tests/49_ed25519.oh
run 50_p256       0  std/core.oh std/p256.oh tests/50_p256.oh
run 51_sha_long   0  std/core.oh std/sha256.oh tests/51_sha_long.oh

echo "== freestanding (zero-dependency, -nostdlib + rt) =="
run_fs 15_freestanding 0 std/mem.oh std/map.oh tests/15_freestanding.oh

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
run stdlib_mem 0 std/mem.oh tests/stdlib/test_mem.oh
run stdlib_json 0 std/core.oh std/str.oh std/json.oh tests/stdlib/test_json.oh
run stdlib_vec  0 std/mem.oh std/vec.oh tests/stdlib/test_vec.oh
run stdlib_math 0 std/math.oh tests/stdlib/test_math.oh
run stdlib_buf  0 std/core.oh std/mem.oh std/buf.oh tests/stdlib/test_buf.oh
run stdlib_map  0 std/mem.oh std/map.oh tests/stdlib/test_map.oh

echo "-------------------------------------------"
echo "RESULT: $pass passed, $fail failed  (arch=$ARCH)"
[ $fail -eq 0 ] || { echo "FAILED: ${failed[*]}"; exit 1; }
echo "ALL GREEN"
