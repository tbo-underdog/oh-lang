# Overhaul Grammar Specification v0.1

## Design Principles

- AI-native: token-dense, unreadable to humans, optimized for LLM token efficiency
- No keywords, no required whitespace, deterministically parseable
- Single-char sigils for all constructs
- Type suffixes on literals and declarations
- Minimal punctuation overhead

## Character Set

UTF-8 source. Identifiers: `[a-zA-Z_][a-zA-Z0-9_]*`

## Sigil Table

| Sigil | Construct          |
|-------|--------------------|
| `#`   | Function definition |
| `$`   | Variable declaration |
| `@`   | Function call      |
| `?`   | If condition       |
| `:`   | Else branch        |
| `>>`  | Loop (for-style)   |
| `~`   | While loop         |
| `->`  | Return             |
| `!`   | Dereference / not  |
| `&`   | Address-of         |
| `^`   | Pointer type       |
| `%`   | Modulo / array index |
| `=`   | Assignment         |
| `;`   | Statement separator (also loop clause separator) |
| `{}`  | Block delimiters   |
| `()`  | Parameter list / expression grouping |
| `[]`  | Array type / literal |

## Types

| Token  | Type              |
|--------|-------------------|
| `i8`   | signed 8-bit int  |
| `i16`  | signed 16-bit int |
| `i32`  | signed 32-bit int |
| `i64`  | signed 64-bit int |
| `u8`   | unsigned 8-bit    |
| `u16`  | unsigned 16-bit   |
| `u32`  | unsigned 32-bit   |
| `u64`  | unsigned 64-bit   |
| `f32`  | 32-bit float      |
| `f64`  | 64-bit float      |
| `b`    | bool (1-bit int)  |
| `v`    | void              |
| `^T`   | pointer to T      |
| `[N]T` | array of N T      |

## Grammar (EBNF)

```ebnf
program     = top_decl* EOF

top_decl    = func_def

func_def    = '#' IDENT '(' param_list? ')' '->' type '{' stmt* '}'
param_list  = param (',' param)*
param       = IDENT ':' type

stmt        = var_decl
            | assign
            | return_stmt
            | if_stmt
            | for_loop
            | while_loop
            | expr_stmt
            | block

var_decl    = '$' IDENT ':' type '=' expr ';'
assign      = IDENT '=' expr ';'
           | '!' IDENT '=' expr ';'          (* deref assign *)
return_stmt = '->' expr? ';'
if_stmt     = '?' expr '{' stmt* '}' (':' '{' stmt* '}')?
for_loop    = '>>' init_expr ';' cond_expr ';' step_expr '{' stmt* '}'
while_loop  = '~' expr '{' stmt* '}'
expr_stmt   = expr ';'
block       = '{' stmt* '}'

init_expr   = var_decl_no_semi | assign_no_semi | epsilon
var_decl_no_semi = '$' IDENT ':' type '=' expr
assign_no_semi   = IDENT '=' expr
cond_expr   = expr
step_expr   = assign_no_semi | expr

expr        = or_expr
or_expr     = and_expr ('||' and_expr)*
and_expr    = eq_expr ('&&' eq_expr)*
eq_expr     = rel_expr (('=='|'!=') rel_expr)*
rel_expr    = add_expr (('<'|'>'|'<='|'>=') add_expr)*
add_expr    = mul_expr (('+'|'-') mul_expr)*
mul_expr    = unary_expr (('*'|'/'|'%') unary_expr)*
unary_expr  = '-' unary_expr
            | '!' unary_expr
            | '&' IDENT
            | primary

primary     = INT_LIT
            | FLOAT_LIT
            | BOOL_LIT
            | func_call
            | array_lit
            | array_idx
            | '(' expr ')'
            | IDENT

func_call   = '@' IDENT '(' arg_list? ')'
arg_list    = expr (',' expr)*
array_lit   = '[' expr (',' expr)* ']'
array_idx   = IDENT '%' expr           (* arr%i means arr[i] *)

BOOL_LIT    = 'T' | 'F'
INT_LIT     = [0-9]+
FLOAT_LIT   = [0-9]+ '.' [0-9]+
IDENT       = [a-zA-Z_][a-zA-Z0-9_]*
```

## Operator Precedence (high to low)

1. Unary: `-` `!` (deref) `&` (addr-of)
2. Multiplicative: `*` `/` `%%` (mod)
3. Additive: `+` `-`
4. Shift: `<<` `>>`
5. Relational: `<` `>` `<=` `>=`
6. Equality: `==` `!=`
7. Bitwise AND: `&`
8. Bitwise OR: `|`
9. Logical AND: `&&`
10. Logical OR: `||`

**Note**: `&` (bitwise AND) has LOWER precedence than `==`. Unlike C.
Parenthesize bitwise ops in comparisons: `(a&mask)==mask` not `a&mask==mask`.

## Examples

### Add function
```
#add(a:i32,b:i32)->i32{->a+b;}
```

### Absolute value
```
#abs(x:i32)->i32{?x<0{->-x;}:{->x;}}
```

### Sum 1..N
```
#sum(n:i32)->i32{$s:i32=0;>>$i:i32=1;i<=n;i=i+1{s=s+i;}->s;}
```

### Fibonacci
```
#fib(n:i32)->i32{?n<=1{->n;}->@fib(n-1)+@fib(n-2);}
```

### Max of array
```
#maxarr(a:^i32,n:i32)->i32{$m:i32=a%0;>>$i:i32=1;i<n;i=i+1{?a%i>m{m=a%i;}}->m;}
```

## Module / Program Entry

The `main` function is the entry point:
```
#main()->i32{...->0;}
```

## Comments

Line comments: `//` to end of line.
Block comments: not supported (AI doesn't need them).

## Decisions Log

- No keywords: sigils (`#`, `$`, `@`, `?`, `>>`, `~`, `->`) replace all keywords
- Array indexing via `%` operator avoids `[` ambiguity with array literals
- No semicolons required after closing `}` of blocks
- For-loop uses `;;` to separate init/cond/step (same char as stmt separator)
- Types are lowercase 2-3 chars for density
- Boolean literals `T`/`F` are single chars
- No string type in v0.1 (syscall-level I/O via byte arrays)
- Pointer arithmetic: `^T` type + `&`/`!` for addr/deref
- All integers are explicitly sized — no default int
- No implicit conversions
