#!/bin/bash
# Generate LLVM IR for all test files using --emit-ir flag

COMP="$(dirname "$0")/../compiler/overhaul"
TESTS="$(dirname "$0")"

for name in 01_add 02_abs 03_sum 04_fibonacci 05_max_array; do
    echo "=== Generating IR for ${name} ==="
    "$COMP" --emit-ir "$TESTS/${name}.oh" /tmp/ir_${name} 2>&1
    echo "exit=$?"
done

echo ""
echo "=== Generated .ll files ==="
ls /tmp/ir_*.ll 2>&1
