# Overhaul Language Specification (AI Reference)

This document teaches an AI to **read and write Overhaul** completely. Overhaul
is an AI-native systems language: token-dense source, static types, no garbage
collector, no mandatory standard library, compiles to native code via LLVM.
Humans are not expected to read raw Overhaul — the `ohtranslate` tool converts
it to readable pseudo-code (see README). This spec is the source of truth for
the syntax; load it before generating Overhaul.

---

## 1. Mental model

- One file or many compiled together as ONE whole program (`overhaul a.oh b.oh ...`).
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
| `6` | i64 | | `v` | void |

- Pointer: `*T`  (e.g. `*3` = pointer to i32, `*1` = pointer to i8 / byte string)
- Array: `[N]T` (e.g. `[16]3` = array of 16 i32, `[256]1` = 256-byte buffer)
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

## 10. Standard library (`std/`, opt-in, dead-code-eliminated)

Compile the modules you use alongside your program; unused functions cost 0 bytes.

- **std/core**: `slen(s)` `bcopy(d,off,s)` `memcpy(d,s,n)` `memset(d,v,n)` `memcmp(a,b,n)` `itoa(d,off,n)`
- **std/str**: `streq(a,b)` `starts(s,p)` `atoi(s)` `sfind(haystack,needle)`
- **std/io**: `print(s)` `eprint(s)` `printn(n)`
- **std/net**: `connect_to(a,b,c,e,port)` (→i64 fd) · `status(buf)` (parse HTTP status)

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
overhaul app.oh -o app                         # native x86-64 binary
overhaul std/core.oh std/io.oh app.oh -o app   # link stdlib modules
overhaul --target aarch64-linux --emit-ir app.oh -o app   # emit ARM64 IR
overhaul --emit-ir app.oh -o app               # keep the .ll for inspection
```

Freestanding (zero-dependency) build: `--emit-ir`, then
`clang -nostdlib -static <start.s> app.ll -o app` (x86-64), or for ARM64:
`clang --target=aarch64-linux-gnu -nostdlib -static -fuse-ld=lld <start_arm.s> app.ll -o app`.
