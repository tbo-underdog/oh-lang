# Oh Language Specification (AI Reference)

This document teaches an AI to **read and write Oh** completely. Oh
is an AI-native systems language: token-dense source, static types, no garbage
collector, no mandatory standard library, compiles to native code via LLVM.
Humans are not expected to read raw Oh — the `ohtranslate` tool converts
it to readable pseudo-code (see README). This spec is the source of truth for
the syntax; load it before generating Oh.

---

## 1. Mental model

- One file or many compiled together as ONE whole program (`oh a.oh b.oh ...`).
- A program is a list of function definitions. `main` is the entry point; its
  return value becomes the process exit code.
- Everything is statically typed. The default scalar type is **i32**.
- The compiler emits LLVM IR → native binary. With `-nostdlib` it is freestanding
  (zero dependencies) on Linux x86-64 and ARM64.

## 2. Lexical rules

- **Whitespace is insignificant** except that a **newline ends a statement**.
  Write **one statement per line**. (Spaces/tabs are ignored.)
- Line comments: `// ... ` to end of line. No block comments.
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`.
- `T` (capital T) is the boolean literal `true`.

## 3. Types (single-token codes)

| code | type | | code | type |
|---|---|---|---|---|
| `1` | i8 | | `f` | f32 |
| `2` | i16 | | `d` | f64 |
| `3` | i32 (default) | | `b` | bool |
| `c` | *i8 (string/byte pointer) |
| `6` | i64 | | `v` | void |

- Pointer: `*T`  (e.g. `*3` = pointer to i32, `*1` = pointer to i8 / byte string)
- Array: `[N]T` (e.g. `[16]3` = array of 16 i32, `[256]1` = 256-byte buffer)
- Struct: `$Name{field:type field:type ...}` defines a named record (commas
  optional). E.g. `$V{data:*3 len:3 cap:3}`. Then `v:V` declares one,
  `v.len` reads a field, `v.len=10` (and `v.len+=1`) writes it. A struct name
  is itself a type, usable for fields, locals, etc. Typed fields remove the
  cast noise of positional state arrays — bind a pointer field once
  (`k:=v.data`, `k` is typed `*3`) then index it directly (`k[i]`).
  - Field access also works **through a pointer**: a `v:*Vec` parameter uses the
    same `v.field` / `v.field=x` syntax (auto-deref, like C's `->`).
  - A struct value **decays to its address** when passed to a `*Struct`
    parameter (like an array → pointer). So a mutating, functional-style API
    reads `vec_push(v, h, x)` at the call site (no `&`), with the callee taking
    `v:*Vec`. This is how the `std/vec` and `std/map` state is passed.
- Legacy long names (`i32`, `u8`, `f64`, …) are still accepted but the codes are preferred.

## 4. Functions

```
#name(params) body
```
- `#` introduces a function. Return type defaults to **i32**; annotate a
  different one with `:` after the params: `#f(...)` returns i32, `#f(...):v{...}`
  returns void, `#f(...):6{...}` returns i64.
- **Params default to i32.** Forms (densest first):
  - `a`      → `a : i32`
  - `*a`     → `a : *i32` (pointer-to-i32; arrays decay to this when passed)
  - `a:T`    → explicit type `T` (e.g. `s:*1` = pointer to i8)
- **Body** is either a single expression (implicit return) or a `{ ... }` block.
- **Implicit return**: the last expression in a body is returned.
- Explicit return: `\expr` (backslash). Bare `\` returns void.

Examples:
```
#add(a,b) a+b                                  // two i32 params, returns i32
#abs(x) x<0?-x:x                               // ternary
#fib(n) n<=1?n:fib(n-1)+fib(n-2)               // recursion, implicit return
#sum(n){                                       // block body
s:=0
@i=1,n+1{s=s+i}
s}
#fill(*a,n):v{@i,n{a[i]=i}}                    // void, pointer param
```

## 5. Variables

- `name:=expr`        → declare, **infer type** from expr.
- `name:T=expr`       → declare with explicit type.
- `name:T`            → declare **uninitialized** (e.g. `buf:[4096]1` scratch buffer).
- `name=expr`         → assign existing variable.
- **Array fill**: `name:[N]T = v` fills all N elements with scalar `v` (e.g. `buf:[256]1=0`).
- **Compound assignment**: `+= -= *= /= &= |= ^=` on a variable, array element
  (`a[i]+=x`), or deref (`!p+=x`). Sugar for `x = x OP e`.
- **Implicit integer widening**: a narrower int auto-widens to a wider one (i32→i64, etc.)
  in arithmetic, calls, assignments, and returns (never narrows — that needs `(T)`).
- Integer literals are **polymorphic**: a literal adopts the int type it is used
  with (so `b[0]=65` stores i8, `fd<0` compares against i64, etc.).

## 6. Control flow

- **If / else**:
  ```
  ?cond{ ...then... }
  ?cond{ ...then... }:{ ...else... }
  ```
- **Ternary expression**: `cond ? a : b` (value; lazy — only the taken branch runs).
- **For loop** (`@`), loop variable defaults to i32:
  ```
  @i,n{ ... }              // i from 0 to n-1  (most common)
  @i=s,e{ ... }            // i from s to e-1
  @i=s,e,step{ ... }       // explicit step
  ```
- **While loop** (`~`): `~cond{ ... }`
- Loop variables follow block scope; you may reuse `i`/`j`/`v` in nested loops.
- **Tail recursion is optimized**: a self-call in tail position (the direct
  return value, incl. a ternary arm) with scalar arguments is compiled to a
  loop — no stack growth, safe to recurse millions deep. Prefer tail/accumulator
  recursion for depth. (Tree recursion like naive `fib` is not loop-converted.)

> **GOTCHA — one statement per line.** A statement whose value is an expression,
> immediately followed by `?` on the **same line**, is misparsed as a ternary.
> Always put an `?`-if on its own line. (This is why bodies are written one
> statement per line.)

## 7. Operators

`+ - * / %`  (arith, `%`=modulo) · `== != < > <= >=` · `&& ||` (logical) ·
`& | ^ ~` (bitwise AND/OR/XOR/NOT) · `<< >>` (shift) · unary `-` (neg), `!` (deref),
`&` (address-of), `~` (bitnot in expression position).

Precedence (high→low): unary · `* / %` · `+ -` · `<< >>` · `< > <= >=` · `== !=` ·
`&` · `^` · `|` · `&&` · `||`.
> Note: bitwise `&` is **lower** than `==` (unlike C). Parenthesize: `(x&m)==m`.

## 8. Pointers, arrays, casts

- `&x` address-of; `!p` dereference (load); `!p=v` store through pointer.
- `a[i]` index (read); `a[i]=v` index assign (write). Works on arrays and pointers.
- Array literal: `[1,5,3,2,4]`. Element type follows the declared array type.
- Cast: `(T)expr` — e.g. `(1)x` truncates to i8, `(3)b` sign-extends i8→i32.

## 9. Builtins (compiler intrinsics — no import needed)

- `sys(num, a1..a6)` → raw syscall, returns i64. Source uses **Linux x86-64
  syscall numbers**; the compiler remaps them per target (x86-64 `syscall`,
  ARM64 `svc #0`). Args may be ints or pointers.
  - Common numbers: read=0 write=1 close=3 mmap=9 socket=41 connect=42 accept=43
    bind=49 listen=50 setsockopt=54 exit=60.
- `popcount(x)` `clz(x)` `ctz(x)` `bswap(x)` → one hardware instruction each.
- SIMD reductions over i32 pointers/arrays (compiler emits a vectorized loop —
  no need to hand-write one): `vsum(a, n)` → sum of `a[0..n)`; `dot(a, b, n)` →
  sum of `a[i]*b[i]` for `i<n`. Both return i32; `n<=0` returns 0.

## 10. Standard library (`std/`, opt-in, dead-code-eliminated)

Compile the modules you use alongside your program; unused functions cost 0 bytes.

- **std/core**: `slen(s)` `bcopy(d,off,s)` `memcpy(d,s,n)` `memset(d,v,n)` `memcmp(a,b,n)` `itoa(d,off,n)`
- **std/str**: `streq(a,b)` `starts(s,p)` `atoi(s)` `sfind(haystack,needle)`
- **std/io**: `print(s)` `eprint(s)` `printn(n)`
- **std/net**: `connect_to(a,b,c,e,port)` (→i64 fd) · `status(buf)` (parse HTTP status)
- **std/buf**: growable byte buffer / string builder on a heap (needs std/mem+core). State is a `Buf` (struct); pass it directly — it decays to `*Buf`. `buf_new(b,h,cap)` `buf_byte(b,h,c)` `buf_str(b,h,s)` `buf_int(b,h,n)` `buf_ptr(b)` `buf_len(b)`. A string builder's final length is unknown at creation, so buf grows (in place via hrealloc); the append fns take the heap `h`.
- **std/map**: fixed-capacity int->int hash map on a heap (needs std/mem; cap = power of two and must exceed the entry count). State is a `Map`. `map_new(m,h,cap)` `map_set(m,k,v)` `map_get(m,k)` (-1 if absent) `map_has(m,k)` `map_count(m)`.
- **std/vec**: fixed-capacity i32 array on a heap (needs std/mem). State is a `Vec`. `vec_new(v,h,cap)` `vec_push(v,x)` `vec_get(v,i)` `vec_set(v,i,x)` `vec_len(v)` `vec_pop(v)`. Size is chosen at creation (the Oh model: you know the size when you make it); push does not grow — it aborts past `cap`.
- **std/math**: `iabs` `imin` `imax` `clamp(x,lo,hi)` `ipow(base,exp)` `isqrt(n)`.
- **std/json**: `json_int(buf,key)` (signed int field, -1 if absent) · `json_has(buf,key)`. Flat-object field extraction, no allocation.
- **std/mem**: mmap-backed bump heap; `h` is a `[3]6` state array, no globals — the caller holds it. `heap_new(h,bytes)` · `halloc(h,n)` (→i64 addr; cast e.g. `(*3)halloc(h,40)`) · `hused(h)`. Reclaim by scope: `heap_reset(h)` (free everything) · `heap_mark(h)`/`heap_restore(h,m)` (free back to a saved point — a bump arena). On exhaustion (or mmap failure) the allocator aborts (exit 137) rather than corrupting memory; size the heap for the work.

## 11. Complete worked example — TCP echo of a fixed HTTP response

```
#main(){
fd:=sys(41,2,1,0)                 // socket(AF_INET, SOCK_STREAM, 0)
?fd<0{\1}
one:=1
sys(54,fd,1,2,&one,4)             // setsockopt SO_REUSEADDR
sa:[16]1=[2,0,31,144,0,0,0,0,0,0,0,0,0,0,0,0]   // sockaddr_in :8080
?sys(49,fd,&sa,16)<0{\2}          // bind
sys(50,fd,16)                     // listen
resp:="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"
n:=slen(resp)
~1{                               // accept loop
c:=sys(43,fd,0,0)
?c<0{\3}
sys(1,c,resp,n)                   // write
sys(3,c)}                         // close
0}
```

## 12. Build commands

```
oh app.oh -o app                         # native x86-64 binary
oh std/core.oh std/io.oh app.oh -o app   # link stdlib modules
oh --target aarch64-linux --emit-ir app.oh -o app   # emit ARM64 IR
oh --emit-ir app.oh -o app               # keep the .ll for inspection
```

Freestanding (zero-dependency) build: `--emit-ir`, then link the start stub **and
`tooling/rt.ll`** (self-contained memset/memcpy/memmove/memcmp — clang can
synthesize calls to these from loops, and a `-nostdlib` build has no libc to
supply them):
`clang -nostdlib -static tooling/start_x86_64.s tooling/rt.ll app.ll -o app` (x86-64), or ARM64:
`clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld tooling/start_arm64.s tooling/rt.ll app.ll -o app`.
The result has zero dynamic dependencies (`file` → "statically linked"), no libc.
