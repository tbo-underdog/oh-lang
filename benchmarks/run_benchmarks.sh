#!/bin/bash
# Overhaul vs C vs Zig Benchmark Script
# Compiles with -O2/-O3 and measures both time and peak RSS memory

COMPILER="$(dirname "$0")/../compiler/overhaul"
BENCH_DIR="$(dirname "$0")"
TMPDIR_BASE="${TMPDIR:-/tmp}"

CC="${CC:-cc}"
ZIG="${ZIG:-zig}"

benchmarks=("add" "abs" "sum" "fibonacci" "max_array")

# ---------------------------------------------------------------------------
# Helper: measure time (nanoseconds) and peak RSS (KB) for a binary
#
# Usage: run_timed <binary>
# Sets globals: _elapsed_ns  _peak_kb
# ---------------------------------------------------------------------------
run_timed() {
    local bin="$1"
    local time_file
    time_file=$(mktemp /tmp/btime_XXXXXX)

    if /usr/bin/time -v "$bin" > /dev/null 2> "$time_file"; then
        :
    fi

    # Parse peak RSS from GNU time -v output
    _peak_kb=$(grep -i "Maximum resident" "$time_file" | awk '{print $NF}')
    [ -z "$_peak_kb" ] && _peak_kb=0

    # We still need wall-clock time; re-run with date for consistency
    local t_start t_end
    t_start=$(date +%s%N)
    "$bin" > /dev/null 2>&1
    t_end=$(date +%s%N)
    _elapsed_ns=$(( t_end - t_start ))

    rm -f "$time_file"
}

# ---------------------------------------------------------------------------
# Table 1: Timing
# ---------------------------------------------------------------------------
printf "\n=== TABLE 1: Execution Time ===\n"
printf "%-15s %12s %12s %12s %10s\n" "Benchmark" "Overhaul(s)" "C(s)" "Zig(s)" "OH/C"
printf "%-15s %12s %12s %12s %10s\n" "----------" "-----------" "----" "------" "----"

declare -A oh_times
declare -A c_times
declare -A zig_times
declare -A oh_mem
declare -A c_mem
declare -A zig_mem

for name in "${benchmarks[@]}"; do
    oh_src="${BENCH_DIR}/bench_${name}.oh"
    c_src="${BENCH_DIR}/bench_${name}.c"
    zig_src="${BENCH_DIR}/bench_${name}.zig"
    oh_bin="${TMPDIR_BASE}/oh_bench_${name}_$$"
    c_bin="${TMPDIR_BASE}/c_bench_${name}_$$"
    zig_bin="${TMPDIR_BASE}/zig_bench_${name}_$$"

    # Compile Overhaul
    compile_ok=1
    "$COMPILER" "$oh_src" "$oh_bin" 2>/dev/null || compile_ok=0

    # Compile C
    c_ok=1
    "$CC" -O2 -o "$c_bin" "$c_src" 2>/dev/null || c_ok=0

    # Compile Zig
    zig_ok=1
    if command -v "$ZIG" > /dev/null 2>&1; then
        "$ZIG" build-exe -O ReleaseFast -femit-bin="$zig_bin" "$zig_src" > /dev/null 2>&1 || zig_ok=0
    else
        zig_ok=0
    fi

    # Measure Overhaul
    if [ "$compile_ok" -eq 1 ]; then
        run_timed "$oh_bin"
        oh_ns=$_elapsed_ns
        oh_mem[$name]=$_peak_kb
        oh_sec=$(awk "BEGIN{printf \"%.3f\", ${oh_ns}/1000000000}")
    else
        oh_sec="ERR"
        oh_mem[$name]=0
        oh_ns=0
    fi

    # Measure C
    if [ "$c_ok" -eq 1 ]; then
        run_timed "$c_bin"
        c_ns=$_elapsed_ns
        c_mem[$name]=$_peak_kb
        c_sec=$(awk "BEGIN{printf \"%.3f\", ${c_ns}/1000000000}")
    else
        c_sec="ERR"
        c_mem[$name]=0
        c_ns=0
    fi

    # Measure Zig
    if [ "$zig_ok" -eq 1 ]; then
        run_timed "$zig_bin"
        zig_ns=$_elapsed_ns
        zig_mem[$name]=$_peak_kb
        zig_sec=$(awk "BEGIN{printf \"%.3f\", ${zig_ns}/1000000000}")
    else
        zig_sec="N/A"
        zig_mem[$name]=0
        zig_ns=0
    fi

    # Ratio OH/C
    if [ "$compile_ok" -eq 1 ] && [ "$c_ok" -eq 1 ] && [ "$c_ns" -gt 0 ]; then
        ratio=$(awk "BEGIN{printf \"%.2fx\", ${oh_ns}/${c_ns}}")
    else
        ratio="N/A"
    fi

    printf "%-15s %12s %12s %12s %10s\n" \
        "${name}" "${oh_sec}" "${c_sec}" "${zig_sec}" "${ratio}"

    # Store for later tables
    oh_times[$name]=$oh_ns
    c_times[$name]=$c_ns
    zig_times[$name]=$zig_ns

    rm -f "$oh_bin" "$c_bin" "$zig_bin" "${zig_bin}.o" 2>/dev/null
done

echo ""
echo "Note: Ratio < 1.0x = Overhaul faster than C"
echo "Overhaul backend: LLVM IR via clang -O3"
echo "C column: ${CC} -O2"

# ---------------------------------------------------------------------------
# Table 2: Binary size (bytes)
# ---------------------------------------------------------------------------
printf "\n=== TABLE 2: Binary Size ===\n"
printf "%-15s %14s %14s %14s\n" "Benchmark" "OH_size(B)" "C_size(B)" "Zig_size(B)"
printf "%-15s %14s %14s %14s\n" "----------" "----------" "---------" "-----------"

for name in "${benchmarks[@]}"; do
    oh_src="${BENCH_DIR}/bench_${name}.oh"
    c_src="${BENCH_DIR}/bench_${name}.c"
    zig_src="${BENCH_DIR}/bench_${name}.zig"
    oh_bin="${TMPDIR_BASE}/soh_bench_${name}_$$"
    c_bin="${TMPDIR_BASE}/sc_bench_${name}_$$"
    zig_bin="${TMPDIR_BASE}/szig_bench_${name}_$$"

    "$COMPILER" "$oh_src" "$oh_bin" 2>/dev/null
    "$CC" -O2 -o "$c_bin" "$c_src" 2>/dev/null

    zig_ok=1
    if command -v "$ZIG" > /dev/null 2>&1; then
        "$ZIG" build-exe -O ReleaseFast -femit-bin="$zig_bin" "$zig_src" > /dev/null 2>&1 || zig_ok=0
    else
        zig_ok=0
    fi

    oh_sz=0; [ -f "$oh_bin" ] && oh_sz=$(wc -c < "$oh_bin" 2>/dev/null || echo 0)
    c_sz=0;  [ -f "$c_bin"  ] && c_sz=$(wc -c < "$c_bin"  2>/dev/null || echo 0)
    zig_sz="N/A"; [ "$zig_ok" -eq 1 ] && [ -f "$zig_bin" ] && \
        zig_sz=$(wc -c < "$zig_bin" 2>/dev/null || echo 0)

    printf "%-15s %14s %14s %14s\n" "${name}" "${oh_sz}" "${c_sz}" "${zig_sz}"

    rm -f "$oh_bin" "$c_bin" "$zig_bin" "${zig_bin}.o" 2>/dev/null
done

# ---------------------------------------------------------------------------
# Table 3: Peak RSS memory
# ---------------------------------------------------------------------------
printf "\n=== TABLE 3: Peak RSS Memory ===\n"
printf "%-15s %14s %14s %14s %18s\n" "Benchmark" "OH_mem(KB)" "C_mem(KB)" "Zig_mem(KB)" "OH_savings_vs_C"
printf "%-15s %14s %14s %14s %18s\n" "----------" "----------" "---------" "-----------" "---------------"

for name in "${benchmarks[@]}"; do
    oh_kb=${oh_mem[$name]:-0}
    c_kb=${c_mem[$name]:-0}
    zig_kb=${zig_mem[$name]:-0}

    # savings: positive means OH uses less than C
    if [ "$c_kb" -gt 0 ] && [ "$oh_kb" -gt 0 ]; then
        savings=$(awk "BEGIN{
            diff = ${c_kb} - ${oh_kb}
            pct  = diff * 100.0 / ${c_kb}
            if (diff >= 0)
                printf \"+%dKB (%.1f%% less)\", diff, pct
            else
                printf \"%dKB (%.1f%% more)\", diff, -pct
        }")
    else
        savings="N/A"
    fi

    zig_kb_disp=$zig_kb
    [ "$zig_kb" -eq 0 ] && zig_kb_disp="N/A"

    printf "%-15s %14s %14s %14s %18s\n" \
        "${name}" "${oh_kb}" "${c_kb}" "${zig_kb_disp}" "${savings}"
done

echo ""
echo "Memory measured with: /usr/bin/time -v (Maximum resident set size)"
echo "Values reflect peak RSS during a full run of each benchmark binary."
