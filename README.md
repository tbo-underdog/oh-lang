# Oh

*Oh — why didn't we think of this sooner?*

**An AI-native systems language.** Token-dense source, static types, no garbage
collector, no mandatory standard library. Compiles through LLVM to native code
and produces freestanding, zero-dependency binaries. Source files end in `.oh`.

Oh is built on one premise: **the programmer is an AI, the reviewer is a
human.** So the source is optimized for the lowest possible token cost (cheaper
for an AI to write and process), and a decoder turns it back into readable
pseudo-code for humans to verify. You do not read raw Oh; you read the
decoded form and the tests.

> Status: **v1**. Honest scope below — this is a young compiler with a real but
> limited platform surface. It is not a finished ecosystem.

---

## What you get

- **Fewer tokens than C** for the same program (−16% to −27% on compute, up to
  −54% on workloads using the built-in vector/struct features) — see
  [BENCHMARKS.md](BENCHMARKS.md).
- **C-class performance** — faster than or equal to C on the majority of
  benchmarks (14 of 15 within the ≤1.5× bar), slower on a few. We publish the
  losses, not just the wins.
- **Static types with structs** — `$Name{field:type …}` records (typed fields,
  pass-by-pointer), plus SIMD reduction builtins (`dot`, `vsum`) that lower to
  vectorized loops.
- **Freestanding binaries** — no libc, fully static, tiny (a 9 KB HTTP server with
  zero dependencies). Ideal for `FROM scratch` containers.
- **A bump-allocator memory model** sized at creation, with bounds-checked
  allocation (aborts rather than corrupts) and scope-based reclamation.
- **A decoder** (`ohtranslate`) so humans can always read what the AI wrote.

## Where it runs (honest)

| Platform | Status |
|---|---|
| **Linux x86-64** | ✅ Fully supported, native, tested |
| **Linux ARM64 (aarch64)** | ✅ Supported; validated under qemu (static ELF, standard syscalls) |
| macOS | ❌ Not yet (needs libSystem backend + Apple hardware to verify) |
| Windows | ❌ Not supported (no raw-syscall ABI; needs a different backend) |
| Bare metal / WASM / other arches | ❌ Not yet |

Realistic deployment today: **Linux servers, cloud VMs (incl. ARM like AWS
Graviton), and containers** — especially as zero-dependency static binaries.

## Build

Requires a C compiler and `clang` (LLVM). ARM64 cross-builds also need `lld` and
`qemu-user-static` to run/verify.

```sh
make -C compiler            # builds ./compiler/oh
make -C translator          # builds ./translator/ohtranslate (the decoder)

# compile and run a program
./compiler/oh tests/01_add.oh -o /tmp/add && /tmp/add; echo $?   # -> 7

# link with stdlib modules (only what you call is included)
./compiler/oh std/core.oh std/io.oh myapp.oh -o myapp
```

Run the full test suite (the GREEN check):

```sh
./run_tests.sh              # native x86-64
ARCH=arm64 ./run_tests.sh   # cross-compile + run under qemu-aarch64
```

## Reading Oh (the decoder)

Raw Oh is dense on purpose. To read it, decode it:

```sh
./translator/ohtranslate tests/04_fibonacci.oh
```
```
function fib(n: i32) -> i32 {
    return n <= 1 ? n : fib(n - 1) + fib(n - 2);
}
```

## Teaching an AI to write Oh

The entire language is specified for AI consumption in one file: [SPEC.md](SPEC.md).
To make any capable coding model fluent in Oh, load that spec into its
context. The simplest command:

```sh
cat SPEC.md          # pipe/paste this into your AI's context
```

Ready-to-paste instruction for an agent:

> Read `SPEC.md` in full — it is the complete Oh language reference. From
> now on, write all code in Oh according to that spec. Compile with
> `./compiler/oh <files...> -o out`, prove correctness with
> `./run_tests.sh`, and when a human needs to read your code, decode it with
> `./translator/ohtranslate <file.oh>`.

That single file is enough for an AI to read and write Oh at a high level —
no fine-tuning or examples beyond the spec are required.

## The AI development workflow

Oh is meant to be driven by an AI under human direction, using strict TDD.
The division of labor:

**The human owns intent and the definition of done.**
1. You write the **product story** — what the software should do.
2. You define the **base test cases**: the inputs and the exact pass/fail signal
   (in Oh, a test passes iff the program compiles **and** its exit code
   matches the expected value — see `run_tests.sh`). You do not need to read
   Oh to know if a test passed; you read the green/red result.

**The AI owns the implementation, via RED → CODE → GREEN.**
3. **RED** — tests exist and fail (the behavior isn't built yet).
4. **CODE** — the AI writes the Oh implementation.
5. **GREEN** — the tests pass. `run_tests.sh` is the arbiter.

The AI may also *write* tests (it's faster), **as long as the human understands
what a true pass and a true fail look like** — which is why the pass/fail signal
is a single, unambiguous exit code, and why every program can be decoded back to
readable form for review. If you can't tell a real green from a faked one, stop
and decode the test.

## Contributing

This repository is the **canonical base.** It is MIT-licensed (see
[LICENSE](LICENSE)) — fork it, modify it, build on it freely.

We accept fixes and optimizations **only if they can be verified on the approved
platforms.** A change is mergeable when:

1. `./run_tests.sh` is **fully green on x86-64**, and
2. `ARCH=arm64 ./run_tests.sh` is **fully green on ARM64** (qemu), and
3. **every feature is benchmarked for BOTH tokens and performance vs C** —
   add a `benchmarks/fair/<name>.{oh,c}` pair and run `benchmarks/bench.sh`
   (it reports token count and fold-proof timing against C for each), and
4. it does not increase token cost on existing programs without a clear,
   measured justification, and any performance claim is backed by those numbers.

> **Rule:** correctness alone is not enough. Two checks gate every change —
> `run_tests.sh` (the GREEN check, both arches) **and** `benchmarks/bench.sh`
> (the TOKENS + PERFORMANCE check vs C). A feature isn't done until both have
> been run and the numbers recorded — wins *and* losses, honestly.

Changes that can't be verified on the approved platforms won't be merged into the
base — not because they're unwelcome, but because the base only ships what we can
prove. New platforms are added one at a time, each behind its own verified green.

## License

MIT © 2026 Craig Milliron. See [LICENSE](LICENSE).
