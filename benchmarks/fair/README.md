# Fold-proof benchmarks (current v3.1 syntax, reproducible)

Each program varies its input with the loop counter and accumulates the result,
so neither compiler can fold the work away. Build the .oh with `overhaul` and the
.c with `cc -O3 -march=native`, then time best-of-N.

bitflip.oh hand-rolls popcount (loses to C 1.46x — clang idiom-recognizes C's loop,
not ours). bitflip_builtin.oh uses the popcount() builtin (wins 0.45x — the
idiomatic way, equivalent to C's __builtin_popcount).
