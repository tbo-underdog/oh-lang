# Conway's Game of Life — in Oh

A live, animated cellular automaton: a 64×32 toroidal grid seeded with gliders,
rendered to the terminal via raw `write`/`nanosleep` syscalls. No libc, no
dependencies.

## Build & run

```sh
# normal build
./compiler/oh projects/13_life/life_core.oh projects/13_life/life.oh -o life
./life          # Ctrl-C to stop

# freestanding: a ~9 KB statically-linked, zero-dependency binary
./compiler/oh projects/13_life/life_core.oh projects/13_life/life.oh --emit-ir -o life
clang -O3 -nostdlib -static tooling/start_x86_64.s tooling/rt.ll life.ll -o life
file life       # -> "statically linked"  (no libc.so)
```

The same source cross-compiles to ARM64 (`--target aarch64-linux`, link with
`start_arm64.s`), verified under qemu.

## Layout
- `life_core.oh` — the engine (`nlive` neighbor count, `step` rules). No `main`;
  compiled alongside a driver, like a stdlib module.
- `life.oh` — the animated demo (seed gliders, render frame, game loop).
- `tests/33_life.oh` — verifies the engine on known patterns (blinker oscillates,
  block is a still life) and runs in the main test suite.

The four rules: a live cell with 2–3 live neighbors survives; a dead cell with
exactly 3 live neighbors is born; everything else dies. Gliders crawl diagonally
forever on the toroidal grid.
