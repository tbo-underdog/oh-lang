# Benchmarks — Oh vs C (honest)

**Methodology.** All speed numbers are *fold-proof*: inputs vary with the loop
counter and results are accumulated and returned, so neither compiler can
optimize the work away. Oh and C are both compiled at maximum optimization
(`-O3 -march=native`; Oh goes through the same clang/LLVM backend). `OH/C` < 1.0
means Oh is faster. Token counts use the `cl100k_base` tokenizer, **reachable-only**
(just the functions used from `main`, including any std modules pulled in).

Reproduce: `benchmarks/bench.sh` (one `.oh` + `.c` pair per benchmark in
`benchmarks/fair/`). **Note on noise:** several workloads run in well under a
millisecond, where `bench.sh`'s best-of-4 wall-clock is noisy; the ratios below
for those rows are confirmed with a median-of-21 A/B. We publish losses, not just
wins.

## Speed (OH/C, lower = Oh faster)

| benchmark | OH/C | result |
|---|---|---|
| sum | 0.06× | **OH faster** |
| json (field extraction) | 0.06× | **OH faster** |
| add | 0.19× | **OH faster** |
| lsearch | 0.37× | **OH faster** |
| abs | 0.49× | **OH faster** |
| strscan (byte scan, mutable buffer) | 0.66× | **OH faster** |
| bitflip_builtin (`popcount()` intrinsic) | 0.67× | **OH faster** |
| vec (1M push + sum, fixed cap) | 0.87× | **OH faster** |
| noalias (single-array kernel) | 0.91× | **OH faster** |
| simd (`dot` builtin, vectorized) | 0.94× | **OH faster** |
| fdot (f64 dot product) | 0.99× | ≈tie (C-class float) |
| map (1M int→int set + get) | 1.06× | ≈tie |
| struct (field read/write loop) | 1.09× | within bar |
| buf (1M int appends, growable) | 1.16× | within bar |
| max_array (small-array hot loop) | 1.16× | within bar |
| math (ipow/isqrt loop) | 1.25× | within bar |
| bitflip (hand-rolled popcount loop) | 1.64× | **over bar — see note** |
| fibonacci (naive tree recursion) | 1.90× | **over bar — justified below** |

**Acceptance bar: OH/C ≤ 1.5× (faster or roughly equal), or justified.**
**16 of 18 within the bar; the two over (fibonacci, hand-rolled bitflip) are explained below.**

> **bitflip, the honest pair:** `bitflip` hand-rolls a 32-iteration popcount loop;
> clang idiom-recognizes the *C* version into a single `popcnt` (so C is fast,
> Oh 1.64×), while Oh's loop stays a loop. `bitflip_builtin` uses Oh's `popcount()`
> intrinsic instead — which flips it to **0.67× (faster than C)** and is ~50× faster
> in absolute time than the hand-rolled loop. That contrast is exactly why the
> builtin exists: don't rely on the backend recognizing a hand-written idiom.

### Why Oh wins where it wins
Stdlib/helpers and SIMD builtins are compiled *with* your program
(`internal fastcc`), so LLVM **inlines and specializes** them and can
**constant-fold** pure calls — things an opaque libc call never gets. The `dot`
builtin lowers to a vectorized reduction (AVX-512 under `-march=native`); `noalias`
(a sound `restrict` on sole scalar-pointer params) frees the vectorizer.

### Why Oh loses where it loses (causes, not excuses)
- **fibonacci (1.90×)** — clang applies a binary-recursion→iteration transform to
  C's `fib` that it won't apply to our IR despite matched attributes. Narrow clang
  heuristic; only bites naive exponential recursion.
- **math / max_array / struct / buf (1.06–1.25×)** — all within the bar. `buf`
  serialization (int→string) is the closest watch item: Oh's `buf_int` is a
  non-inlined call with data-dependent division loops; an inlining hint would close
  the residual gap. `max_array` is small-array store-to-load forwarding.

## Tokens (OH vs equivalent C, reachable-only)

| group | typical Δ vs C |
|---|---|
| compute (add/abs/sum/bitflip/lsearch/…) | **−16% to −27%** |
| language features (struct −47%, noalias −44%, simd −54%) | **−44% to −54%** |
| data-structure stdlib (vec/map/buf/json) | **+146% to +260%** (see note) |

Compute and the new builtins are unconditionally cheaper than C. The
data-structure modules show *more* tokens than C **only because they bundle Oh's
heap allocator**, which C gets for free from libc (`malloc`/`realloc` are not in
the source and not counted). The application/algorithm code itself is competitive;
the allocator (~50–90 reachable tokens) is shared infrastructure that amortizes to
~0 across programs that reuse it.

## Binary size & dependencies

A freestanding build links no libc. The HTTP web server:

| | Oh (freestanding) | C (libc) |
|---|---|---|
| binary size | 9,056 bytes | 16,272 bytes |
| dynamic dependencies | none (static) | libc.so.6 |

## Memory model (v2)

`std/mem` is an mmap-backed **bump allocator**: one syscall up front, then
allocation is a pointer bump. It is *correct for the Oh model* — size known at
creation, scoped lifetimes, with truly-unbounded data offloaded externally — and
beats `malloc`'s generality for that pattern. Safety: `halloc` bounds-checks and
**aborts (137) on exhaustion rather than corrupting memory**; `heap_reset` /
`heap_mark` / `heap_restore` reclaim by scope (the only free path). `vec` is
fixed-capacity (size chosen at creation, no grow); `buf` grows (a string builder's
length is unknown at creation) via in-place `hrealloc`.

## Honest bottom line

Oh is **faster than or equal to C on the majority of these benchmarks, cheaper in
tokens on all compute and language-feature workloads, and produces smaller
zero-dependency binaries.** It is **not uniformly faster**: it loses on naive
exponential recursion (fibonacci, 1.90×) and is a touch behind on a few in-RAM
data-structure workloads (1.06–1.25×, within the bar). Data-structure modules read
as more tokens than C purely because they ship the allocator C borrows from libc.
If your priority is minimum token/compute cost with C-class (often better)
performance and zero-dependency binaries, the trade is favorable — and we don't
claim it wins every micro-benchmark.
