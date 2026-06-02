# Overhaul v0.1 — Implementation Decisions

## Compiler Language: C (not Zig)

**Decision**: Wrote the compiler in C11, not Zig.

**Reason**: Zig 0.17.0-dev (the installed version) has a completely reworked build and stdlib API
that diverges significantly from stable 0.13/0.14 documentation. The `Build.ExecutableOptions`
struct requires `root_module: *Module` (not `root_source_file`); `std.heap.GeneralPurposeAllocator`
was renamed to `std.heap.DebugAllocator`; `std.process.argsAlloc` was removed in favor of
`std.process.Args` with an `Io` context object; `std.process.Child.run` requires an `Io` parameter.
These are API-breaking changes in the dev build that would require significant adaptation time.
C11 with a simple arena allocator achieves the same goals with zero dependency friction.

## Codegen Backend: LLVM IR (replaces C emission)

**Decision**: Emits LLVM IR text (`.ll`), compiled by
`clang -O3 -fno-plt -march=native`, rather than C code via `cc -O2`.

**Implementation**: `codegen.c` uses the alloca/load/store idiom — every local variable
gets an `alloca` slot; reads go through `load`, writes through `store`.  This avoids
having to build an explicit SSA phi-node graph.  Clang's `mem2reg` pass (part of `-O3`)
promotes all allocas to registers before code generation, so the output is fully
optimized.  Key IR mapping:

| Overhaul type | LLVM IR type |
|---------------|--------------|
| `i32`/`i64`   | `i32`/`i64`  |
| `f64`         | `double`     |
| `^T` (pointer)| `ptr` (opaque, LLVM 15+) |
| `[N]T` (array)| `[N x T]` (alloca); `ptr` at call boundary |
| bool          | `i1`         |

Arrays passed to functions decay to `ptr` at the call site (the callee receives the
base address via a `ptr` alloca slot, and element access uses `getelementptr` after
loading the pointer).  Loop constructs lower to labelled basic blocks with conditional
`br` instructions.  The intermediate `.ll` file is deleted after compilation.

**Why now**: `llc` is absent from PATH but `clang` (18.1.3) accepts `.ll` files
directly and applies the full LLVM optimization pipeline.

**Performance**: Benchmark results show Overhaul (LLVM IR -O3 -march=native) is
significantly faster than the C reference compiled at -O2:
- `fibonacci`: 0.002 s vs 6.447 s (0.00x — fully inlined/unrolled at -O3)
- `sum`:       0.001 s vs 0.002 s (0.44x)
- All other benchmarks: sub-millisecond vs tens-of-milliseconds

The improvement comes from `-O3` (vs `-O2`) plus `-march=native` (native SIMD/ISA
extensions), not from IR emission per se — the previous C backend could be updated
to match, but LLVM IR emission is now the canonical path.

## Test Output via Exit Code

**Decision**: Programs return results via exit code (0-255) rather than stdout.

**Reason**: No standard library means no printf. Adding a printf builtin would require either
syscall-level I/O (complex, platform-specific) or declaring printf as a foreign function
(needs a foreign-function interface not in v0.1 scope). Exit codes are sufficient for
numeric correctness checks in the test suite.

## Array-to-Pointer Decay

**Decision**: Arrays implicitly coerce to pointers when passed as function arguments.

**Reason**: This is required to call `maxarr(a:^i32, n:i32)` with a local `[5]i32` array.
The typechecker allows `[N]T` -> `^T` coercion at call sites. The codegen emits C, where
this decay is automatic. This is consistent with C's memory model.

## Cross-Platform Target Support

**Decision**: Added a `--target <triple>` CLI flag that controls the LLVM target triple
and datalayout emitted in the IR, and the clang invocation used to compile it.

**Supported targets**:

| Flag               | LLVM triple                   | Notes                            |
|--------------------|-------------------------------|----------------------------------|
| `x86_64-linux`     | `x86_64-unknown-linux-gnu`    | Default; native Linux x86-64     |
| `x86_64-macos`     | `x86_64-apple-macosx12.0`     | Intel Mac                        |
| `x86_64-windows`   | `x86_64-pc-windows-msvc`      | Windows MSVC ABI                 |
| `aarch64-linux`    | `aarch64-unknown-linux-gnu`   | ARM64 Linux                      |
| `aarch64-macos`    | `aarch64-apple-macosx12.0`    | Apple Silicon                    |

**IR changes**: Each target now emits both `target datalayout` and `target triple` in
the module header.  Datalayout strings encode endianness, ABI pointer sizes, and
alignment requirements per platform (System V AMD64, Mach-O, COFF/MSVC, AArch64 ELF,
Apple AArch64 Mach-O).

**Clang invocation**:
- Native (`x86_64-linux`): `clang -O3 -fno-plt -target x86_64-unknown-linux-gnu`
  (`-march=native` removed — binaries now portable across x86-64 generations)
- Cross targets: `clang -O3 --target=<llvm-triple>` (uses clang's cross driver)

**Bonus flag `--emit-ir`**: Keeps the intermediate `.ll` file on disk (printed to
stderr) instead of removing it after compilation.  Useful for inspecting generated IR.

**Cross-compilation requirements**: aarch64 and Windows targets require a matching
clang cross-compilation toolchain and sysroot to be installed (e.g.
`clang-18` with the relevant multilib or `clang-cross-aarch64-linux-gnu` package).
If the toolchain is absent, clang emits a descriptive error and the compiler prints
a hint about sysroot configuration.  The compiler does not fail silently — it reports
the target triple in the error message and advises on the missing toolchain.

**Why `-march=native` was removed**: The flag binds the binary to the specific CPU
micro-architecture of the build host, making the output non-redistributable and
incompatible with cross-compilation.  `-O3` alone still enables auto-vectorisation
and full optimisation without micro-architecture lock-in.

## Tools Available

```
zig       0.17.0-dev.389+f5a1968f6  (not used — API too unstable)
clang     18.1.3                     (LLVM IR compiler: clang -O3 -fno-plt -target <triple>)
gcc       13.3.0                     (not used)
llc-18    available but not in PATH  (not used — clang compiles .ll directly)
rustc     available                  (not used)
```
