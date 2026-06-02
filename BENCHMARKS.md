# Benchmarks — Overhaul vs C (honest)

**Methodology.** All speed numbers are *fold-proof*: inputs vary with the loop
counter and results are accumulated and returned, so neither compiler can
optimize the work away. Overhaul and C are both compiled at maximum
optimization (`-O3 -march=native`; Overhaul goes through the same clang/LLVM
backend). Times are best-of-4 wall-clock on the same x86-64 Linux machine.
`OH/C` < 1.0 means Overhaul is faster.

We do **not** hide losses. Overhaul wins most of these and is consistently
cheaper in tokens, but C is faster on several and we say so.

## Speed — computation

| benchmark | OH (s) | C (s) | OH/C | result |
|---|---|---|---|---|
| add | 0.0139 | 0.0726 | 0.19× | **OH faster** |
| abs | 0.0027 | 0.0052 | 0.51× | **OH faster** |
| sum | 0.0006 | 0.0091 | 0.07× | **OH faster** |
| lsearch | 0.0109 | 0.0293 | 0.37× | **OH faster** |
| fibonacci | 8.77 | 4.52 | 1.94× | C faster |
| max_array | 0.122 | 0.028 | 4.32× | C faster |
| bitflip (hand-rolled popcount) | 0.157 | 0.108 | 1.45× | C faster |

## Speed — standard library vs libc

| function | OH (s) | libc (s) | OH/C | result |
|---|---|---|---|---|
| memcpy | 0.0189 | 0.0258 | 0.73× | **OH faster** |
| memset | 0.0020 | 0.0088 | 0.22× | **OH faster** |
| slen / strlen | 0.0002 | 0.0004 | 0.40× | **OH faster** |
| atoi | 0.0001 | 0.0254 | 0.01× | **OH faster** |
| memcmp | 0.0225 | 0.0148 | 1.52× | libc faster |

**Tally: Overhaul beats C/libc on 8 of 12, loses on 4.**

### Why Overhaul wins where it wins
Stdlib/helpers are compiled *with* your program (`internal fastcc`), so LLVM
**inlines and specializes** them and can **constant-fold** pure calls — things
an opaque libc call can never get. That is the whole advantage.

### Why Overhaul loses where it loses (no excuses, just causes)
- **fibonacci (1.94×)** — clang applies a binary-recursion→iteration transform to
  C's `fib` that it will not apply to our IR despite matched attributes. Narrow
  clang heuristic; only bites naive exponential recursion.
- **max_array (4.32×)** — a tiny 5-element array rescanned in a hot loop. C keeps
  it in registers; our codegen keeps it in memory. A real register-allocation gap
  for small-array-heavy loops.
- **bitflip (1.45×)** — this row uses a *hand-rolled* 32-iteration popcount. Using
  the `popcount()` builtin instead (one `popcnt` instruction) makes Overhaul win
  outright; the loss is only if you hand-roll it.
- **memcmp (1.52×)** — libc's is hand-tuned assembly with early-exit. Ours
  vectorizes (full-scan, no early exit) — much closer than it was (4×→1.5×) but
  still behind libc's tuned scan.

## Tokens (the consistent win)

Measured with the `cl100k_base` tokenizer. Overhaul source vs equivalent C.

| comparison | Overhaul | C | difference |
|---|---|---|---|
| 5 core functions (add/abs/sum/fib/maxarr) | 87 | 110 | **−21%** |
| REST client application code | 230 | 304 | **−24%** |
| stdlib function implementations (5) | 130 | 139 | −6% |

Tokens are where Overhaul is unconditionally ahead: it expresses the same
programs in fewer tokens than C, and with a shared stdlib the per-program cost
drops further (helpers are written once and dead-code-eliminated).

## Binary size & dependencies

A freestanding build links no libc. The HTTP web server:

| | Overhaul (freestanding) | C (libc) |
|---|---|---|
| binary size | 9,056 bytes | 16,272 bytes |
| dynamic dependencies | none (static) | libc.so.6 |

## Honest bottom line

Overhaul is **faster than C on the majority of benchmarks, cheaper in tokens on
all of them, and produces smaller zero-dependency binaries** — but it is **not
uniformly faster**. It loses to C on naive deep recursion, small-array hot loops,
and libc's hand-tuned `memcmp`. If your priority is minimum token/compute cost
with C-class (and often better) performance, that trade is favorable. If you need
to win every micro-benchmark, it does not — and we will not claim it does.

*Reproduce:* sources in `benchmarks/`; build both, run fold-proof loops, compare.
