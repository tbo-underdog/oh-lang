#!/bin/bash
# Overhaul Test Runner
# Compiles each .oh file, runs binary, checks exit code against expected value

COMPILER="$(dirname "$0")/../compiler/overhaul"
TESTS_DIR="$(dirname "$0")"
PASS=0
FAIL=0

run_test() {
    local name="$1"
    local oh_file="$2"
    local expected="$3"
    local bin="/tmp/oh_test_$$_${name}"

    # Compile
    "$COMPILER" "$oh_file" "$bin" 2>/tmp/oh_compile_err_$$
    if [ $? -ne 0 ]; then
        echo "[FAIL] ${name}: compile error"
        cat /tmp/oh_compile_err_$$
        FAIL=$((FAIL + 1))
        return
    fi

    # Run and capture exit code
    "$bin"
    local got=$?

    # Cleanup
    rm -f "$bin" /tmp/oh_compile_err_$$

    if [ "$got" -eq "$expected" ]; then
        echo "[PASS] ${name}: got ${got}"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] ${name}: expected ${expected}, got ${got}"
        FAIL=$((FAIL + 1))
    fi
}

run_test "01_add"       "$TESTS_DIR/01_add.oh"       7
run_test "02_abs"       "$TESTS_DIR/02_abs.oh"        5
run_test "03_sum"       "$TESTS_DIR/03_sum.oh"        55
run_test "04_fibonacci" "$TESTS_DIR/04_fibonacci.oh"  55
run_test "05_max_array" "$TESTS_DIR/05_max_array.oh"  5

TOTAL=$((PASS + FAIL))
echo "${PASS}/${TOTAL} tests passed"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
exit 0
