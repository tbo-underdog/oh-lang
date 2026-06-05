/*
 * codegen.c — LLVM IR text emitter for Oh
 *
 * Strategy: alloca/load/store for all locals (SSA mem2reg promoted by
 * clang's optimizer).  Every expression returns a fresh %tmp_N register.
 * Labels use a global counter to stay unique across blocks.
 */

#include "codegen.h"
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static FILE *out;

/* Active target arch, set by codegen(). Drives per-arch syscall lowering. */
static TargetTriple g_target;

/* Set transiently when emitting a call that is the direct value of a return
 * (tail position). Consumed (and cleared) at the top of EX_CALL. */
static int g_tail;
static int target_is_aarch64(void) {
    return g_target == TARGET_AARCH64_LINUX || g_target == TARGET_AARCH64_MACOS;
}

/* Oh's syscall ABI is "Linux x86-64 numbers". For other targets the
 * codegen remaps a constant syscall number to that target's number, so .oh
 * source stays portable and token-identical across arches. Linux x86-64 ->
 * Linux aarch64 mapping for the calls our programs/stdlib use. */
static long remap_syscall_x86_to_arm64(long n) {
    switch (n) {
    case 0:  return 63;   /* read */
    case 1:  return 64;   /* write */
    case 2:  return 1024; /* open -> openat-ish; rarely used, placeholder */
    case 3:  return 57;   /* close */
    case 9:  return 222;  /* mmap */
    case 11: return 215;  /* munmap */
    case 35: return 101;  /* nanosleep */
    case 41: return 198;  /* socket */
    case 42: return 203;  /* connect */
    case 43: return 202;  /* accept */
    case 44: return 206;  /* sendto */
    case 45: return 207;  /* recvfrom */
    case 49: return 200;  /* bind */
    case 50: return 201;  /* listen */
    case 54: return 208;  /* setsockopt */
    case 60: return 93;   /* exit */
    case 72: return 25;   /* fcntl */
    case 228: return 113; /* clock_gettime */
    case 233: return 21;  /* epoll_ctl */
    case 281: return 22;  /* epoll_pwait (arm64 has no epoll_wait) */
    case 288: return 242; /* accept4 */
    case 291: return 20;  /* epoll_create1 */
    case 318: return 278; /* getrandom */
    default: return n;    /* assume same (read/write family already differ above) */
    }
}

/* SSA register counter – each new value gets a unique number */
static int reg_ctr;

/* Label counter */
static int lbl_ctr;

/* ------------------------------------------------------------------ */
/* Inline-constant register encoding                                   */
/*                                                                     */
/* Registers >= CONST_REG_BASE are "virtual constant registers":       */
/* they carry a value that can be emitted inline in instructions       */
/* without a preceding assignment.  This eliminates "add i32 0, N"    */
/* noise and enables compile-time constant folding.                    */
/* ------------------------------------------------------------------ */
#define CONST_REG_BASE 0x40000000
#define IS_CONST(r)    ((r) >= CONST_REG_BASE)
#define CONST_IDX(r)   ((r) - CONST_REG_BASE)

#define MAX_CONST_REGS 4096
typedef struct { int64_t ival; double fval; bool is_float; } ConstEntry;
static ConstEntry const_table[MAX_CONST_REGS];
static int        const_count;  /* reset per function */

static int new_iconst(int64_t v) {
    assert(const_count < MAX_CONST_REGS);
    int idx = const_count++;
    const_table[idx].ival = v; const_table[idx].is_float = false;
    return CONST_REG_BASE + idx;
}
static int new_fconst(double v) {
    assert(const_count < MAX_CONST_REGS);
    int idx = const_count++;
    const_table[idx].fval = v; const_table[idx].is_float = true;
    return CONST_REG_BASE + idx;
}

/* Emit a value reference — either %tN or an inline constant */
static void emit_ref(int r) {
    if (IS_CONST(r)) {
        ConstEntry *ce = &const_table[CONST_IDX(r)];
        if (ce->is_float) {
            /* Emit the exact IEEE-754 bits as LLVM hex float — valid for both
             * `double` and `float`, and never drops the point (3.0 -> "3" was
             * an invalid integer constant in a float context). */
            union { double d; unsigned long long u; } b; b.d = ce->fval;
            fprintf(out, "0x%016llX", b.u);
        }
        else              fprintf(out, "%lld", (long long)ce->ival);
    } else {
        fprintf(out, "%%t%d", r);
    }
}

/* ------------------------------------------------------------------ */
/* String literal table                                                */
/* ------------------------------------------------------------------ */

#define MAX_STRINGS 256
typedef struct { const char *text; int idx; } StrEntry;
static StrEntry str_table[MAX_STRINGS];
static int      str_count = 0;

static int str_intern(const char *text) {
    for (int i = 0; i < str_count; i++)
        if (strcmp(str_table[i].text, text) == 0) return str_table[i].idx;
    assert(str_count < MAX_STRINGS);
    int idx = str_count;
    str_table[str_count].text = text;
    str_table[str_count].idx  = idx;
    str_count++;
    return idx;
}

static void collect_strings_expr(Expr *e);
static void collect_strings_stmts(Stmt **body, size_t n);

static void collect_strings_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EX_STRLIT:   str_intern(e->sval); break;
    case EX_BINOP:    collect_strings_expr(e->binop.lhs); collect_strings_expr(e->binop.rhs); break;
    case EX_UNOP:     collect_strings_expr(e->unop.operand); break;
    case EX_CAST:     collect_strings_expr(e->cast.operand); break;
    case EX_TERNARY:  collect_strings_expr(e->ternary.cond);
                      collect_strings_expr(e->ternary.then_e);
                      collect_strings_expr(e->ternary.else_e); break;
    case EX_CALL:
        for (size_t i2 = 0; i2 < e->call.argc; i2++) collect_strings_expr(e->call.args[i2]);
        break;
    case EX_ARRAYLIT:
        for (size_t i2 = 0; i2 < e->arrlit.elemc; i2++) collect_strings_expr(e->arrlit.elems[i2]);
        break;
    case EX_ARRAYIDX: collect_strings_expr(e->arridx.idx); break;
    default: break;
    }
}

static void collect_strings_stmts(Stmt **body, size_t n) {
    for (size_t i2 = 0; i2 < n; i2++) {
        Stmt *s = body[i2];
        switch (s->kind) {
        case ST_VARDECL:     collect_strings_expr(s->vardecl.init); break;
        case ST_ASSIGN:      collect_strings_expr(s->assign.rhs); break;
        case ST_DEREFASSIGN: collect_strings_expr(s->derefassign.rhs); break;
        case ST_IDXASSIGN:
            collect_strings_expr(s->idxassign.idx);
            collect_strings_expr(s->idxassign.rhs); break;
        case ST_FIELDASSIGN: collect_strings_expr(s->fieldassign.rhs); break;
        case ST_RETURN:      collect_strings_expr(s->ret.val); break;
        case ST_EXPRSTMT:    collect_strings_expr(s->exprstmt); break;
        case ST_IF:
            collect_strings_expr(s->ifst.cond);
            collect_strings_stmts(s->ifst.then_body, s->ifst.then_len);
            if (s->ifst.else_body) collect_strings_stmts(s->ifst.else_body, s->ifst.else_len);
            break;
        case ST_FOR:
            if (s->forst.init_init) collect_strings_expr(s->forst.init_init);
            if (s->forst.init_rhs)  collect_strings_expr(s->forst.init_rhs);
            collect_strings_expr(s->forst.cond);
            if (s->forst.step_rhs)  collect_strings_expr(s->forst.step_rhs);
            if (s->forst.step_expr) collect_strings_expr(s->forst.step_expr);
            collect_strings_stmts(s->forst.body, s->forst.body_len);
            break;
        case ST_WHILE:
            collect_strings_expr(s->whilest.cond);
            collect_strings_stmts(s->whilest.body, s->whilest.body_len);
            break;
        }
    }
}

/* ---- SIMD reduction builtin usage scan ----
 * vsum/dot are lowered to module-level helper functions (so their loop lives in
 * its own entry block — mem2reg-promotable and clang-vectorizable — regardless
 * of the call site's context). Scan the program to know which to emit. */
static int g_uses_vsum = 0, g_uses_dot = 0;
static void scan_simd_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EX_CALL:
        if (strcmp(e->call.name,"vsum")==0) g_uses_vsum = 1;
        else if (strcmp(e->call.name,"dot")==0) g_uses_dot = 1;
        for (size_t i=0;i<e->call.argc;i++) scan_simd_expr(e->call.args[i]);
        break;
    case EX_BINOP:   scan_simd_expr(e->binop.lhs); scan_simd_expr(e->binop.rhs); break;
    case EX_UNOP:    scan_simd_expr(e->unop.operand); break;
    case EX_CAST:    scan_simd_expr(e->cast.operand); break;
    case EX_TERNARY: scan_simd_expr(e->ternary.cond); scan_simd_expr(e->ternary.then_e); scan_simd_expr(e->ternary.else_e); break;
    case EX_ARRAYIDX:scan_simd_expr(e->arridx.idx); break;
    case EX_ARRAYLIT:for (size_t i=0;i<e->arrlit.elemc;i++) scan_simd_expr(e->arrlit.elems[i]); break;
    default: break;
    }
}
static void scan_simd_stmts(Stmt **body, size_t n) {
    for (size_t i=0;i<n;i++) {
        Stmt *s = body[i]; if (!s) continue;
        switch (s->kind) {
        case ST_VARDECL: scan_simd_expr(s->vardecl.init); break;
        case ST_ASSIGN:  scan_simd_expr(s->assign.rhs); break;
        case ST_DEREFASSIGN: scan_simd_expr(s->derefassign.rhs); break;
        case ST_IDXASSIGN: scan_simd_expr(s->idxassign.idx); scan_simd_expr(s->idxassign.rhs); break;
        case ST_FIELDASSIGN: scan_simd_expr(s->fieldassign.rhs); break;
        case ST_RETURN:  scan_simd_expr(s->ret.val); break;
        case ST_EXPRSTMT:scan_simd_expr(s->exprstmt); break;
        case ST_IF: scan_simd_expr(s->ifst.cond); scan_simd_stmts(s->ifst.then_body,s->ifst.then_len); scan_simd_stmts(s->ifst.else_body,s->ifst.else_len); break;
        case ST_FOR: scan_simd_expr(s->forst.init_init); scan_simd_expr(s->forst.init_rhs); scan_simd_expr(s->forst.cond); scan_simd_expr(s->forst.step_rhs); scan_simd_expr(s->forst.step_expr); scan_simd_stmts(s->forst.body,s->forst.body_len); break;
        case ST_WHILE: scan_simd_expr(s->whilest.cond); scan_simd_stmts(s->whilest.body,s->whilest.body_len); break;
        }
    }
}

/* Number of decoded bytes a string literal expands to (escapes collapse to 1). */
static size_t str_decoded_len(const char *text) {
    size_t n = 0;
    for (size_t j = 0; text[j]; j++) {
        if (text[j] == '\\' && text[j+1] != '\0') j++; /* escape pair -> 1 byte */
        n++;
    }
    return n;
}

static void emit_string_globals(void) {
    for (int i2 = 0; i2 < str_count; i2++) {
        const char *text = str_table[i2].text;
        size_t raw = strlen(text);
        size_t len = str_decoded_len(text) + 1; /* +1 for NUL; declared array size */
        fprintf(out, "@.str_%d = private unnamed_addr constant [%zu x i8] c\"", i2, len);
        for (size_t j = 0; j < raw; j++) {
            unsigned char c = (unsigned char)text[j];
            if (c == '\\' && text[j+1] != '\0') {
                j++;
                unsigned char ec = (unsigned char)text[j];
                if      (ec == 'n')  fprintf(out, "\\0A");
                else if (ec == 't')  fprintf(out, "\\09");
                else if (ec == 'r')  fprintf(out, "\\0D");
                else if (ec == '0')  fprintf(out, "\\00");
                else if (ec == '\\') fprintf(out, "\\5C");
                else if (ec == '"')  fprintf(out, "\\22");
                else                 fprintf(out, "\\%02X", ec);
            } else if (c >= 32 && c < 127 && c != '"' && c != '\\') {
                fputc(c, out);
            } else {
                fprintf(out, "\\%02X", c);
            }
        }
        fprintf(out, "\\00\", align 1\n");
    }
    if (str_count > 0) fprintf(out, "\n");
}

/* Per-function: map from variable name to alloca register index */
#define MAX_VARS 512
typedef struct {
    const char *name;
    int         reg;        /* the %v_<reg> alloca (-1 if SSA-direct param) */
    int         direct_reg; /* >= 0: variable lives in %t<direct_reg> directly
                               (no alloca); -1 means use alloca at %v<reg>    */
    TypeKind    kind;       /* primitive type stored */
    Type       *type;       /* full type */
    int         scope;      /* block depth when declared; -1 = out of scope   */
} VarSlot;

static VarSlot var_slots[MAX_VARS];
static int     var_count;
static int     var_scope;   /* current block scope depth */

static void vars_reset(void) {
    var_count = 0;
    var_scope = 0;
}

/* Search backward: returns the most-recently-declared in-scope variable */
static int vars_lookup(const char *name) {
    for (int i = var_count - 1; i >= 0; i--)
        if (var_slots[i].scope >= 0 && strcmp(var_slots[i].name, name) == 0)
            return i;
    return -1;
}

/* Push / pop block scope */
static void scope_push(void) { var_scope++; }
static void scope_pop(void) {
    /* Hide all variables declared at current scope depth */
    for (int i = 0; i < var_count; i++)
        if (var_slots[i].scope == var_scope) var_slots[i].scope = -1;
    var_scope--;
}

static int vars_alloc(const char *name, Type *t) {
    assert(var_count < MAX_VARS);
    var_slots[var_count].name       = name;
    var_slots[var_count].type       = t;
    var_slots[var_count].kind       = t->kind;
    var_slots[var_count].reg        = reg_ctr++;
    var_slots[var_count].direct_reg = -1;
    var_slots[var_count].scope      = var_scope;
    return var_count++;
}

/* Register a variable that already has its alloca emitted in the entry block
 * (by the hoist pass). Uses the pre-assigned reg; does NOT bump reg_ctr. */
static int vars_alloc_with_reg(const char *name, Type *t, int reg) {
    assert(var_count < MAX_VARS);
    var_slots[var_count].name       = name;
    var_slots[var_count].type       = t;
    var_slots[var_count].kind       = t->kind;
    var_slots[var_count].reg        = reg;
    var_slots[var_count].direct_reg = -1;
    var_slots[var_count].scope      = var_scope;
    return var_count++;
}

/*
 * Register a parameter that is never mutated.  Instead of allocating stack
 * space we record the incoming SSA argument register (%t<direct_reg>) directly.
 * reg_ctr is NOT incremented — no alloca register is consumed.
 */
static int vars_alloc_direct(const char *name, Type *t, int direct_reg) {
    assert(var_count < MAX_VARS);
    var_slots[var_count].name       = name;
    var_slots[var_count].type       = t;
    var_slots[var_count].kind       = t->kind;
    var_slots[var_count].reg        = -1;
    var_slots[var_count].direct_reg = direct_reg;
    var_slots[var_count].scope      = var_scope;
    return var_count++;
}

/* ------------------------------------------------------------------ */
/* Mutation analysis                                                   */
/*                                                                     */
/* Walk the statement tree and return 1 if 'name' is ever the target  */
/* of an assignment (ST_ASSIGN / ST_DEREFASSIGN / ST_IDXASSIGN / for  */
/* init-assign / for step-assign).                                     */
/* ------------------------------------------------------------------ */

static int stmts_mutate(const char *name, Stmt **body, size_t n);

/* ---- Purity analysis for readnone attribute ----
 * A function is readnone-safe only if it never escapes a pointer, derefs,
 * or calls another function. (Conservative: array indexing on a local is
 * fine, but address-of / deref / calls can touch memory the optimizer must
 * not assume away.) */
static int expr_impure(Expr *e);
static int stmts_impure(Stmt **body, size_t n);

static int expr_impure(Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
    case EX_CALL: return 1;
    case EX_UNOP:
        if (e->unop.op == UOP_ADDROF || e->unop.op == UOP_DEREF) return 1;
        return expr_impure(e->unop.operand);
    case EX_BINOP: return expr_impure(e->binop.lhs) || expr_impure(e->binop.rhs);
    case EX_CAST:  return expr_impure(e->cast.operand);
    case EX_TERNARY: return expr_impure(e->ternary.cond) ||
                            expr_impure(e->ternary.then_e) || expr_impure(e->ternary.else_e);
    case EX_ARRAYIDX: return expr_impure(e->arridx.idx);
    case EX_ARRAYLIT: {
        for (size_t i=0;i<e->arrlit.elemc;i++) if(expr_impure(e->arrlit.elems[i])) return 1;
        return 0;
    }
    default: return 0;
    }
}
static int stmt_impure(Stmt *s) {
    switch (s->kind) {
    case ST_VARDECL:     return expr_impure(s->vardecl.init);
    case ST_ASSIGN:      return expr_impure(s->assign.rhs);
    case ST_DEREFASSIGN: return 1;  /* writes through pointer */
    case ST_IDXASSIGN:   return expr_impure(s->idxassign.idx) || expr_impure(s->idxassign.rhs);
    case ST_RETURN:      return expr_impure(s->ret.val);
    case ST_EXPRSTMT:    return expr_impure(s->exprstmt);
    case ST_IF:          return expr_impure(s->ifst.cond) ||
                                stmts_impure(s->ifst.then_body,s->ifst.then_len) ||
                                stmts_impure(s->ifst.else_body,s->ifst.else_len);
    case ST_FOR:         return expr_impure(s->forst.init_init) || expr_impure(s->forst.init_rhs) ||
                                expr_impure(s->forst.cond) || expr_impure(s->forst.step_rhs) ||
                                expr_impure(s->forst.step_expr) ||
                                stmts_impure(s->forst.body,s->forst.body_len);
    case ST_WHILE:       return expr_impure(s->whilest.cond) ||
                                stmts_impure(s->whilest.body,s->whilest.body_len);
    default:             return 0;
    }
}
static int stmts_impure(Stmt **body, size_t n) {
    for (size_t i=0;i<n;i++) if (body[i] && stmt_impure(body[i])) return 1;
    return 0;
}

/* ---- Interprocedural purity (call-graph fixpoint) ----
 * The simple stmts_impure() treats EVERY call as impure, so a function that
 * only calls *pure* functions (e.g. recursive fib) is wrongly excluded from
 * memory(none). Here we compute purity across the call graph: a function is
 * pure if it touches no escaping memory AND every function it calls is pure.
 * Result lets recursive pure functions (fib) get memory(none), which lets
 * clang optimise the call tree like it does for C. */
#define MAX_FUNCS 1024
static const char *g_pure_names[MAX_FUNCS];
static int         g_pure_flag[MAX_FUNCS];
static int         g_pure_count;

static int func_is_pure(const char *name) {
    for (int i=0;i<g_pure_count;i++)
        if (strcmp(g_pure_names[i],name)==0) return g_pure_flag[i];
    return 0; /* unknown callee → assume impure */
}

/* Local (non-call) memory access: address-of, deref, or any array indexing.
 * These mean the function reads/writes memory, disqualifying memory(none). */
static int expr_mem(Expr *e);
static int expr_mem(Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
    case EX_UNOP:
        if (e->unop.op==UOP_ADDROF || e->unop.op==UOP_DEREF) return 1;
        return expr_mem(e->unop.operand);
    case EX_ARRAYIDX: return 1; /* reads memory */
    case EX_BINOP:    return expr_mem(e->binop.lhs)||expr_mem(e->binop.rhs);
    case EX_CAST:     return expr_mem(e->cast.operand);
    case EX_TERNARY:  return expr_mem(e->ternary.cond)||expr_mem(e->ternary.then_e)||expr_mem(e->ternary.else_e);
    case EX_CALL: { for(size_t i=0;i<e->call.argc;i++) if(expr_mem(e->call.args[i])) return 1; return 0; }
    case EX_ARRAYLIT:{ for(size_t i=0;i<e->arrlit.elemc;i++) if(expr_mem(e->arrlit.elems[i])) return 1; return 0; }
    default: return 0;
    }
}
/* Returns 1 if expr calls any function that is NOT currently marked pure. */
static int expr_calls_impure(Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
    case EX_CALL:
        if (!func_is_pure(e->call.name)) return 1;
        for(size_t i=0;i<e->call.argc;i++) if(expr_calls_impure(e->call.args[i])) return 1;
        return 0;
    case EX_UNOP:    return expr_calls_impure(e->unop.operand);
    case EX_BINOP:   return expr_calls_impure(e->binop.lhs)||expr_calls_impure(e->binop.rhs);
    case EX_CAST:    return expr_calls_impure(e->cast.operand);
    case EX_TERNARY: return expr_calls_impure(e->ternary.cond)||expr_calls_impure(e->ternary.then_e)||expr_calls_impure(e->ternary.else_e);
    case EX_ARRAYIDX:return expr_calls_impure(e->arridx.idx);
    case EX_ARRAYLIT:{for(size_t i=0;i<e->arrlit.elemc;i++) if(expr_calls_impure(e->arrlit.elems[i])) return 1; return 0;}
    default: return 0;
    }
}
static int stmts_mem(Stmt **b,size_t n);
static int stmt_mem(Stmt *s){
    switch(s->kind){
    case ST_VARDECL: return expr_mem(s->vardecl.init);
    case ST_ASSIGN:  return expr_mem(s->assign.rhs);
    case ST_DEREFASSIGN: return 1;
    case ST_IDXASSIGN:   return 1; /* writes memory */
    case ST_RETURN:  return expr_mem(s->ret.val);
    case ST_EXPRSTMT:return expr_mem(s->exprstmt);
    case ST_IF: return expr_mem(s->ifst.cond)||stmts_mem(s->ifst.then_body,s->ifst.then_len)||stmts_mem(s->ifst.else_body,s->ifst.else_len);
    case ST_FOR: return expr_mem(s->forst.init_init)||expr_mem(s->forst.init_rhs)||expr_mem(s->forst.cond)||expr_mem(s->forst.step_rhs)||expr_mem(s->forst.step_expr)||stmts_mem(s->forst.body,s->forst.body_len);
    case ST_WHILE: return expr_mem(s->whilest.cond)||stmts_mem(s->whilest.body,s->whilest.body_len);
    default: return 0;
    }
}
static int stmts_mem(Stmt **b,size_t n){for(size_t i=0;i<n;i++) if(b[i]&&stmt_mem(b[i])) return 1; return 0;}
static int stmts_call_impure(Stmt **b,size_t n);
static int stmt_call_impure(Stmt *s){
    switch(s->kind){
    case ST_VARDECL: return expr_calls_impure(s->vardecl.init);
    case ST_ASSIGN:  return expr_calls_impure(s->assign.rhs);
    case ST_DEREFASSIGN: return expr_calls_impure(s->derefassign.rhs);
    case ST_IDXASSIGN:   return expr_calls_impure(s->idxassign.idx)||expr_calls_impure(s->idxassign.rhs);
    case ST_RETURN:  return expr_calls_impure(s->ret.val);
    case ST_EXPRSTMT:return expr_calls_impure(s->exprstmt);
    case ST_IF: return expr_calls_impure(s->ifst.cond)||stmts_call_impure(s->ifst.then_body,s->ifst.then_len)||stmts_call_impure(s->ifst.else_body,s->ifst.else_len);
    case ST_FOR: return expr_calls_impure(s->forst.init_init)||expr_calls_impure(s->forst.init_rhs)||expr_calls_impure(s->forst.cond)||expr_calls_impure(s->forst.step_rhs)||expr_calls_impure(s->forst.step_expr)||stmts_call_impure(s->forst.body,s->forst.body_len);
    case ST_WHILE: return expr_calls_impure(s->whilest.cond)||stmts_call_impure(s->whilest.body,s->whilest.body_len);
    default: return 0;
    }
}
static int stmts_call_impure(Stmt **b,size_t n){for(size_t i=0;i<n;i++) if(b[i]&&stmt_call_impure(b[i])) return 1; return 0;}

/* Compute purity over the whole program via greatest-fixpoint:
 * start optimistic (all pure), demote any function that has a ptr param,
 * touches memory, or calls an impure function; repeat until stable. */
static void compute_purity(Program *prog) {
    g_pure_count = (int)prog->func_count;
    for (int i=0;i<g_pure_count;i++) {
        g_pure_names[i] = prog->funcs[i].name;
        /* structural disqualifiers (independent of calls) */
        FuncDef *f=&prog->funcs[i];
        int hasptr=0;
        for(size_t j=0;j<f->param_count;j++){TypeKind pk=f->param_types[j]->kind; if(pk==TY_PTR||pk==TY_ARRAY){hasptr=1;break;}}
        g_pure_flag[i] = (!hasptr && !stmts_mem(f->body,f->body_len)) ? 1 : 0;
    }
    int changed=1;
    while(changed){
        changed=0;
        for(int i=0;i<g_pure_count;i++){
            if(!g_pure_flag[i]) continue;
            if(stmts_call_impure(prog->funcs[i].body, prog->funcs[i].body_len)){
                g_pure_flag[i]=0; changed=1;
            }
        }
    }
}

/* ---- willreturn analysis ----
 * A function provably returns to its caller (in finite time) if it has no
 * unbounded `while` loop, makes no blocking `sys` call, and only calls other
 * willreturn functions. `willreturn`+`mustprogress` is what lets LLVM convert
 * recursion into iteration (it must prove termination first) — the fib gap. */
static int g_wret_flag[MAX_FUNCS];

static int func_is_wret(const char *name) {
    if (strcmp(name,"sys")==0) return 0;                 /* may block (accept/read) */
    if (strcmp(name,"popcount")==0||strcmp(name,"clz")==0||
        strcmp(name,"ctz")==0||strcmp(name,"bswap")==0||strcmp(name,"archid")==0) return 1; /* instant intrinsics */
    for (int i=0;i<g_pure_count;i++)
        if (strcmp(g_pure_names[i],name)==0) return g_wret_flag[i];
    return 0;
}
static int stmts_has_while(Stmt**b,size_t n);
static int stmt_has_while(Stmt*s){
    switch(s->kind){
    case ST_WHILE: return 1;
    case ST_IF: return stmts_has_while(s->ifst.then_body,s->ifst.then_len)||stmts_has_while(s->ifst.else_body,s->ifst.else_len);
    case ST_FOR: return stmts_has_while(s->forst.body,s->forst.body_len);
    default: return 0;
    }
}
static int stmts_has_while(Stmt**b,size_t n){for(size_t i=0;i<n;i++) if(b[i]&&stmt_has_while(b[i])) return 1; return 0;}
/* expr calls a non-willreturn function? */
static int expr_calls_nonwret(Expr*e){
    if(!e) return 0;
    switch(e->kind){
    case EX_CALL:
        if(!func_is_wret(e->call.name)) return 1;
        for(size_t i=0;i<e->call.argc;i++) if(expr_calls_nonwret(e->call.args[i])) return 1;
        return 0;
    case EX_UNOP: return expr_calls_nonwret(e->unop.operand);
    case EX_BINOP: return expr_calls_nonwret(e->binop.lhs)||expr_calls_nonwret(e->binop.rhs);
    case EX_CAST: return expr_calls_nonwret(e->cast.operand);
    case EX_TERNARY: return expr_calls_nonwret(e->ternary.cond)||expr_calls_nonwret(e->ternary.then_e)||expr_calls_nonwret(e->ternary.else_e);
    case EX_ARRAYIDX: return expr_calls_nonwret(e->arridx.idx);
    case EX_ARRAYLIT:{for(size_t i=0;i<e->arrlit.elemc;i++) if(expr_calls_nonwret(e->arrlit.elems[i])) return 1; return 0;}
    default: return 0;
    }
}
static int stmts_call_nonwret(Stmt**b,size_t n);
static int stmt_call_nonwret(Stmt*s){
    switch(s->kind){
    case ST_VARDECL: return expr_calls_nonwret(s->vardecl.init);
    case ST_ASSIGN: return expr_calls_nonwret(s->assign.rhs);
    case ST_DEREFASSIGN: return expr_calls_nonwret(s->derefassign.rhs);
    case ST_IDXASSIGN: return expr_calls_nonwret(s->idxassign.idx)||expr_calls_nonwret(s->idxassign.rhs);
    case ST_RETURN: return expr_calls_nonwret(s->ret.val);
    case ST_EXPRSTMT: return expr_calls_nonwret(s->exprstmt);
    case ST_IF: return expr_calls_nonwret(s->ifst.cond)||stmts_call_nonwret(s->ifst.then_body,s->ifst.then_len)||stmts_call_nonwret(s->ifst.else_body,s->ifst.else_len);
    case ST_FOR: return expr_calls_nonwret(s->forst.init_init)||expr_calls_nonwret(s->forst.init_rhs)||expr_calls_nonwret(s->forst.cond)||expr_calls_nonwret(s->forst.step_rhs)||expr_calls_nonwret(s->forst.step_expr)||stmts_call_nonwret(s->forst.body,s->forst.body_len);
    case ST_WHILE: return 1; /* while already disqualifies */
    default: return 0;
    }
}
static int stmts_call_nonwret(Stmt**b,size_t n){for(size_t i=0;i<n;i++) if(b[i]&&stmt_call_nonwret(b[i])) return 1; return 0;}

static void compute_willreturn(Program *prog) {
    for (int i=0;i<g_pure_count;i++)
        g_wret_flag[i] = !stmts_has_while(prog->funcs[i].body, prog->funcs[i].body_len);
    int changed=1;
    while(changed){
        changed=0;
        for(int i=0;i<g_pure_count;i++){
            if(!g_wret_flag[i]) continue;
            if(stmts_call_nonwret(prog->funcs[i].body, prog->funcs[i].body_len)){
                g_wret_flag[i]=0; changed=1;
            }
        }
    }
}

static int stmt_mutates(const char *name, Stmt *s) {
    switch (s->kind) {
    case ST_ASSIGN:
        if (strcmp(s->assign.name, name) == 0) return 1;
        break;
    case ST_DEREFASSIGN:
        if (strcmp(s->derefassign.name, name) == 0) return 1;
        break;
    case ST_IDXASSIGN:
        if (strcmp(s->idxassign.name, name) == 0) return 1;
        break;
    case ST_IF:
        if (stmts_mutate(name, s->ifst.then_body, s->ifst.then_len)) return 1;
        if (s->ifst.else_body &&
            stmts_mutate(name, s->ifst.else_body, s->ifst.else_len)) return 1;
        break;
    case ST_FOR:
        if (s->forst.has_init_assign && strcmp(s->forst.init_name, name) == 0) return 1;
        if (s->forst.has_step_assign && strcmp(s->forst.step_name,  name) == 0) return 1;
        if (stmts_mutate(name, s->forst.body, s->forst.body_len)) return 1;
        break;
    case ST_WHILE:
        if (stmts_mutate(name, s->whilest.body, s->whilest.body_len)) return 1;
        break;
    default:
        break;
    }
    return 0;
}

static int stmts_mutate(const char *name, Stmt **body, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (stmt_mutates(name, body[i])) return 1;
    return 0;
}

/* Returns 1 if parameter 'name' is mutated anywhere in the function body. */
static int param_is_mutated(const char *name, FuncDef *f) {
    return stmts_mutate(name, f->body, f->body_len);
}

/* ------------------------------------------------------------------ */
/* Type → LLVM IR string                                               */
/* ------------------------------------------------------------------ */

/* Writes the LLVM scalar type for a TypeKind (no arrays/ptrs) */
static const char *llvm_scalar(TypeKind k) {
    switch (k) {
    case TY_I8:   return "i8";
    case TY_I16:  return "i16";
    case TY_I32:  return "i32";
    case TY_I64:  return "i64";
    case TY_U8:   return "i8";
    case TY_U16:  return "i16";
    case TY_U32:  return "i32";
    case TY_U64:  return "i64";
    case TY_F32:  return "float";
    case TY_F64:  return "double";
    case TY_BOOL: return "i1";
    case TY_VOID: return "void";
    case TY_PTR:  return "ptr";
    case TY_ARRAY: return "ptr"; /* arrays become ptr in arg/load position */
    case TY_STRUCT: return "ptr"; /* struct value decays to its address in arg position */
    }
    return "i32";
}

/* Signed integer types: signed overflow is UB (as in C), so we tag arithmetic
 * with `nsw` to unlock LLVM transforms like recursion→iteration and induction
 * variable analysis. */
static int is_signed_int(TypeKind k) {
    return k==TY_I8||k==TY_I16||k==TY_I32||k==TY_I64;
}

/* Struct registry (set in codegen()) for field index/type lookup */
static StructDef *cg_structs = NULL;
static size_t     cg_struct_count = 0;
static StructDef *cg_lookup_struct(const char *name) {
    for (size_t i = 0; i < cg_struct_count; i++)
        if (strcmp(cg_structs[i].name, name) == 0) return &cg_structs[i];
    return NULL;
}
/* Field index within a struct (0-based GEP index); -1 if not found */
static int cg_field_index(const char *sname, const char *fname) {
    StructDef *sd = cg_lookup_struct(sname);
    if (!sd) return -1;
    for (size_t i = 0; i < sd->field_count; i++)
        if (strcmp(sd->field_names[i], fname) == 0) return (int)i;
    return -1;
}

/* Write a full LLVM type expression (for alloca / getelementptr etc.) */
static void emit_llvm_type(Type *t) {
    switch (t->kind) {
    case TY_I8:   fprintf(out, "i8");    return;
    case TY_I16:  fprintf(out, "i16");   return;
    case TY_I32:  fprintf(out, "i32");   return;
    case TY_I64:  fprintf(out, "i64");   return;
    case TY_U8:   fprintf(out, "i8");    return;
    case TY_U16:  fprintf(out, "i16");   return;
    case TY_U32:  fprintf(out, "i32");   return;
    case TY_U64:  fprintf(out, "i64");   return;
    case TY_F32:  fprintf(out, "float"); return;
    case TY_F64:  fprintf(out, "double");return;
    case TY_BOOL: fprintf(out, "i1");    return;
    case TY_VOID: fprintf(out, "void");  return;
    case TY_PTR:  fprintf(out, "ptr");   return;
    case TY_ARRAY:
        fprintf(out, "[%llu x ", (unsigned long long)t->array_size);
        emit_llvm_type(t->inner);
        fprintf(out, "]");
        return;
    case TY_STRUCT:
        fprintf(out, "%%%s", t->struct_name);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Fresh register / label helpers                                      */
/* ------------------------------------------------------------------ */

static int new_reg(void)  { return reg_ctr++; }
static int new_label(void){ return lbl_ctr++; }

/* Coerce a GEP element index to i64. Indices may come from i64 arithmetic; the
 * GEP element-index operand is emitted as i64, so a narrower SSA value must be
 * widened first. Constants and already-64-bit values pass through unchanged. */
static int coerce_index_i64(Expr *idxe, int idx_reg) {
    if (IS_CONST(idx_reg)) return idx_reg;
    TypeKind k = (idxe && idxe->typ) ? idxe->typ->kind : TY_I32;
    if (k == TY_I64 || k == TY_U64 || k == TY_PTR) return idx_reg;
    int r = new_reg();
    fprintf(out, "  %%t%d = %s %s %%t%d to i64\n",
            r, is_signed_int(k) ? "sext" : "zext", llvm_scalar(k), idx_reg);
    return r;
}

/* ------------------------------------------------------------------ */
/* Expression emission                                                 */
/*                                                                     */
/* emit_expr() emits the instructions that compute the expression and  */
/* returns the integer register index that holds the result.           */
/* ------------------------------------------------------------------ */

static int emit_expr(Expr *e);

/* Resolve the base pointer of a struct variable for field access. Handles both:
 *   v:Struct       — `v` is a local; base = its alloca address (%v<reg>)
 *   v:*Struct      — `v` is a pointer-to-struct; base = the pointer VALUE
 *                    (loaded from its slot, or the SSA reg for an unmutated param)
 * Writes the struct type name into *out_sname and returns the register/ref that
 * holds the base pointer; *is_vreg tells the caller whether it's a %v alloca
 * (true) or a %t value (false). */
static int struct_base_ref(int slot, const char **out_sname, int *is_vreg) {
    Type *vt = var_slots[slot].type;
    if (vt->kind == TY_PTR) {            /* pointer-to-struct: auto-deref */
        *out_sname = vt->inner->struct_name;
        *is_vreg = 0;
        if (var_slots[slot].direct_reg >= 0)
            return var_slots[slot].direct_reg;       /* pointer already in %t */
        int p = new_reg();                           /* load the pointer */
        fprintf(out, "  %%t%d = load ptr, ptr %%v%d\n", p, var_slots[slot].reg);
        return p;
    }
    /* struct value held in a local alloca */
    *out_sname = vt->struct_name;
    *is_vreg = 1;
    return var_slots[slot].reg;
}

/* Helper: emit an icmp for signed integer comparison, return result reg */
static int emit_icmp(const char *pred, int lr, int rr, TypeKind k) {
    /* Constant-fold at compile time for integer comparisons */
    if (!strchr(pred, 'o') /* no float predicates */ &&
        IS_CONST(lr) && IS_CONST(rr)) {
        int64_t lv = const_table[CONST_IDX(lr)].ival;
        int64_t rv = const_table[CONST_IDX(rr)].ival;
        int result = 0;
        if      (strcmp(pred,"eq") ==0) result = (lv==rv);
        else if (strcmp(pred,"ne") ==0) result = (lv!=rv);
        else if (strcmp(pred,"slt")==0) result = (lv< rv);
        else if (strcmp(pred,"sgt")==0) result = (lv> rv);
        else if (strcmp(pred,"sle")==0) result = (lv<=rv);
        else if (strcmp(pred,"sge")==0) result = (lv>=rv);
        if (result >= 0) return new_iconst(result);
    }
    int res = new_reg();
    /* float operands use fcmp (ordered predicates); integers use icmp */
    int is_fcmp = (k == TY_F32 || k == TY_F64);
    fprintf(out, "  %%t%d = %s %s %s ", res, is_fcmp ? "fcmp" : "icmp", pred, llvm_scalar(k));
    emit_ref(lr); fprintf(out, ", "); emit_ref(rr); fprintf(out, "\n");
    return res;
}

/* Helper: zero-extend i1 → i32 (for bool ops used in arithmetic) */
static int emit_zext_i1_to_i32(int r) {
    if (IS_CONST(r)) return new_iconst(const_table[CONST_IDX(r)].ival ? 1 : 0);
    int res = new_reg();
    fprintf(out, "  %%t%d = zext i1 %%t%d to i32\n", res, r);
    return res;
}

/* Helper: truncate to i1 (for use as br condition) */
static int ensure_i1(int r, TypeKind k) {
    if (k == TY_BOOL) return r;
    if (IS_CONST(r)) return new_iconst(const_table[CONST_IDX(r)].ival != 0 ? 1 : 0);
    int zero = new_reg();
    fprintf(out, "  %%t%d = icmp ne %s %%t%d, 0\n", zero, llvm_scalar(k), r);
    return zero;
}

static int emit_expr(Expr *e) {
    switch (e->kind) {

    case EX_INTLIT:  return new_iconst(e->ival);
    case EX_FLOATLIT: return new_fconst(e->fval);
    case EX_BOOLLIT:  return new_iconst(e->bval ? 1 : 0);

    case EX_IDENT: {
        int idx = vars_lookup(e->ident);
        assert(idx >= 0 && "unknown variable in emit_expr");
        Type *vt = var_slots[idx].type;

        /* SSA-direct param: value already lives in a register, no load needed */
        if (var_slots[idx].direct_reg >= 0) {
            return var_slots[idx].direct_reg;
        }

        int r = new_reg();
        if (vt->kind == TY_STRUCT) {
            /* Struct value used as an r-value (passed to a *Struct param):
             * decay to its address, like an array. */
            fprintf(out, "  %%t%d = getelementptr i8, ptr %%v%d, i32 0\n",
                    r, var_slots[idx].reg);
        } else if (vt->kind == TY_ARRAY) {
            /* Arrays: the alloca holds [N x T].  When used as an r-value
             * (e.g. passed to a function expecting ^T) we return the ptr
             * directly — no load needed. */
            fprintf(out, "  %%t%d = getelementptr [%llu x ",
                    r, (unsigned long long)vt->array_size);
            emit_llvm_type(vt->inner);
            fprintf(out, "], ptr %%v%d, i32 0, i32 0\n",
                    var_slots[idx].reg);
        } else {
            fprintf(out, "  %%t%d = load %s, ptr %%v%d\n",
                    r, llvm_scalar(vt->kind), var_slots[idx].reg);
        }
        return r;
    }

    case EX_FIELD: {
        int idx = vars_lookup(e->field.var);
        assert(idx >= 0 && "unknown struct variable in emit_expr");
        const char *sname; int is_vreg;
        int base = struct_base_ref(idx, &sname, &is_vreg);
        int fi = cg_field_index(sname, e->field.field);
        assert(fi >= 0 && "unknown struct field in emit_expr");
        /* GEP to the field, then load its value */
        int p = new_reg();
        fprintf(out, "  %%t%d = getelementptr %%%s, ptr %s%d, i32 0, i32 %d\n",
                p, sname, is_vreg ? "%v" : "%t", base, fi);
        int r = new_reg();
        fprintf(out, "  %%t%d = load ", r);
        emit_llvm_type(e->typ);
        fprintf(out, ", ptr %%t%d\n", p);
        return r;
    }

    case EX_BINOP: {
        TypeKind lk = e->binop.lhs->typ->kind;
        bool is_float = (lk == TY_F32 || lk == TY_F64);

        /* Short-circuit logical && / || — the RHS must NOT be evaluated when the
         * LHS already decides the result (so guard idioms like `p!=0 && !p>0`
         * or `d!=0 && n/d>k` are safe). Handled before the eager operand emit. */
        if (e->binop.op == OP_AND || e->binop.op == OP_OR) {
            int is_and = (e->binop.op == OP_AND);
            int la = emit_expr(e->binop.lhs);
            int ca = ensure_i1(la, e->binop.lhs->typ->kind);
            if (IS_CONST(ca)) {
                int lv = const_table[CONST_IDX(ca)].ival != 0;
                if (is_and && !lv) return new_iconst(0);
                if (!is_and && lv) return new_iconst(1);
                int rb = emit_expr(e->binop.rhs);
                return ensure_i1(rb, e->binop.rhs->typ->kind);
            }
            int slot = new_reg();
            fprintf(out, "  %%v%d = alloca i1\n", slot);
            int lrhs = new_label(), lshort = new_label(), lend = new_label();
            if (is_and)
                fprintf(out, "  br i1 %%t%d, label %%L%d, label %%L%d\n", ca, lrhs, lshort);
            else
                fprintf(out, "  br i1 %%t%d, label %%L%d, label %%L%d\n", ca, lshort, lrhs);
            fprintf(out, "L%d:\n", lrhs);
            int rb = emit_expr(e->binop.rhs);
            int cb = ensure_i1(rb, e->binop.rhs->typ->kind);
            fprintf(out, "  store i1 "); emit_ref(cb);
            fprintf(out, ", ptr %%v%d\n  br label %%L%d\n", slot, lend);
            fprintf(out, "L%d:\n  store i1 %d, ptr %%v%d\n  br label %%L%d\n",
                    lshort, is_and ? 0 : 1, slot, lend);
            fprintf(out, "L%d:\n", lend);
            int r = new_reg();
            fprintf(out, "  %%t%d = load i1, ptr %%v%d\n", r, slot);
            return r;
        }

        int lr = emit_expr(e->binop.lhs);
        int rr = emit_expr(e->binop.rhs);
        int res = new_reg();

        /* Compile-time integer constant folding */
#define IFOLD2(op) (IS_CONST(lr) && IS_CONST(rr) && !is_float) \
            ? new_iconst(const_table[CONST_IDX(lr)].ival op const_table[CONST_IDX(rr)].ival) \
            : -1
        switch (e->binop.op) {
        case OP_ADD: {
            int fold = IFOLD2(+); if (fold != -1) return fold;
            if (lk == TY_PTR) {
                /* element-scaled pointer arithmetic (C semantics): p+n advances
                 * n elements of the pointee type, not n bytes. */
                Type *pe = e->binop.lhs->typ->inner;
                fprintf(out, "  %%t%d = getelementptr ", res);
                if (pe) emit_llvm_type(pe); else fprintf(out, "i8");
                fprintf(out, ", ptr %%t%d, i32 ", lr);
                emit_ref(rr); fprintf(out, "\n");
            } else if (is_float) {
                fprintf(out, "  %%t%d = fadd %s ", res, llvm_scalar(lk));
                emit_ref(lr); fprintf(out, ", "); emit_ref(rr); fprintf(out, "\n");
            } else {
                fprintf(out, "  %%t%d = add %s%s ", res, is_signed_int(lk)?"nsw ":"", llvm_scalar(lk));
                emit_ref(lr); fprintf(out, ", "); emit_ref(rr); fprintf(out, "\n");
            }
            return res;
        }
        case OP_SUB: {
            int fold = IFOLD2(-); if (fold != -1) return fold;
            if (lk == TY_PTR) {
                int neg_rr;
                if (IS_CONST(rr)) neg_rr = new_iconst(-const_table[CONST_IDX(rr)].ival);
                else { neg_rr = new_reg(); fprintf(out,"  %%t%d = sub i32 0, %%t%d\n",neg_rr,rr); }
                Type *pe = e->binop.lhs->typ->inner;
                fprintf(out, "  %%t%d = getelementptr ", res);
                if (pe) emit_llvm_type(pe); else fprintf(out, "i8");
                fprintf(out, ", ptr %%t%d, i32 ", lr);
                emit_ref(neg_rr); fprintf(out, "\n");
            } else if (is_float) {
                fprintf(out, "  %%t%d = fsub %s ", res, llvm_scalar(lk));
                emit_ref(lr); fprintf(out, ", "); emit_ref(rr); fprintf(out, "\n");
            } else {
                fprintf(out, "  %%t%d = sub %s%s ", res, is_signed_int(lk)?"nsw ":"", llvm_scalar(lk));
                emit_ref(lr); fprintf(out, ", "); emit_ref(rr); fprintf(out, "\n");
            }
            return res;
        }
        case OP_MUL: {
            int fold = IFOLD2(*); if (fold != -1) return fold;
            if (is_float) { fprintf(out,"  %%t%d = fmul %s ",res,llvm_scalar(lk)); }
            else          { fprintf(out,"  %%t%d = mul %s%s ", res,is_signed_int(lk)?"nsw ":"",llvm_scalar(lk)); }
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_DIV: {
            if (!is_float && IS_CONST(lr) && IS_CONST(rr) && const_table[CONST_IDX(rr)].ival != 0)
                return new_iconst(const_table[CONST_IDX(lr)].ival / const_table[CONST_IDX(rr)].ival);
            if (is_float) { fprintf(out,"  %%t%d = fdiv %s ",res,llvm_scalar(lk)); }
            else          { fprintf(out,"  %%t%d = sdiv %s ",res,llvm_scalar(lk)); }
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_MOD: {
            if (!is_float && IS_CONST(lr) && IS_CONST(rr) && const_table[CONST_IDX(rr)].ival != 0)
                return new_iconst(const_table[CONST_IDX(lr)].ival % const_table[CONST_IDX(rr)].ival);
            if (is_float) { fprintf(out,"  %%t%d = frem %s ",res,llvm_scalar(lk)); }
            else          { fprintf(out,"  %%t%d = srem %s ",res,llvm_scalar(lk)); }
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_XOR: {
            int fold = IFOLD2(^); if (fold != -1) return fold;
            fprintf(out,"  %%t%d = xor %s ",res,llvm_scalar(lk));
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_BITOR: {
            int fold = IFOLD2(|); if (fold != -1) return fold;
            fprintf(out,"  %%t%d = or %s ",res,llvm_scalar(lk));
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_BITAND: {
            int fold = IFOLD2(&); if (fold != -1) return fold;
            fprintf(out,"  %%t%d = and %s ",res,llvm_scalar(lk));
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_LSHIFT: {
            int fold = IFOLD2(<<); if (fold != -1) return fold;
            fprintf(out,"  %%t%d = shl %s ",res,llvm_scalar(lk));
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_RSHIFT: {
            int fold = IFOLD2(>>); if (fold != -1) return fold;
            if (lk == TY_U8 || lk == TY_U16 || lk == TY_U32 || lk == TY_U64)
                fprintf(out,"  %%t%d = lshr %s ",res,llvm_scalar(lk));
            else
                fprintf(out,"  %%t%d = ashr %s ",res,llvm_scalar(lk));
            emit_ref(lr); fprintf(out,", "); emit_ref(rr); fprintf(out,"\n"); return res;
        }
        case OP_EQ:  return emit_icmp(is_float ? "oeq" : "eq",  lr, rr, lk);
        case OP_NEQ: return emit_icmp(is_float ? "one" : "ne",  lr, rr, lk);
        case OP_LT:  return emit_icmp(is_float ? "olt" : "slt", lr, rr, lk);
        case OP_GT:  return emit_icmp(is_float ? "ogt" : "sgt", lr, rr, lk);
        case OP_LTEQ:return emit_icmp(is_float ? "ole" : "sle", lr, rr, lk);
        case OP_GTEQ:return emit_icmp(is_float ? "oge" : "sge", lr, rr, lk);
        case OP_AND: {
            int l1 = ensure_i1(lr, lk);
            int r1 = ensure_i1(rr, e->binop.rhs->typ->kind);
            if (IS_CONST(l1) && IS_CONST(r1))
                return new_iconst(const_table[CONST_IDX(l1)].ival && const_table[CONST_IDX(r1)].ival);
            fprintf(out, "  %%t%d = and i1 ", res);
            emit_ref(l1); fprintf(out, ", "); emit_ref(r1); fprintf(out, "\n");
            return res;
        }
        case OP_OR: {
            int l1 = ensure_i1(lr, lk);
            int r1 = ensure_i1(rr, e->binop.rhs->typ->kind);
            if (IS_CONST(l1) && IS_CONST(r1))
                return new_iconst(const_table[CONST_IDX(l1)].ival || const_table[CONST_IDX(r1)].ival);
            fprintf(out, "  %%t%d = or i1 ", res);
            emit_ref(l1); fprintf(out, ", "); emit_ref(r1); fprintf(out, "\n");
            return res;
        }
        }
        break;
    }

    case EX_UNOP: {
        /* Address-of computes a *location*, not a value — handle it before the
         * eager operand emit so we don't load (and don't evaluate an index twice).
         * Supports &name, &arr[i], and &struct.field. */
        if (e->unop.op == UOP_ADDROF) {
            Expr *op = e->unop.operand;
            if (op->kind == EX_IDENT) {
                int idx = vars_lookup(op->ident);
                assert(idx >= 0 && "unknown variable in address-of");
                int r = new_reg();
                fprintf(out, "  %%t%d = getelementptr i8, ptr %%v%d, i32 0\n",
                        r, var_slots[idx].reg);
                return r;
            }
            if (op->kind == EX_ARRAYIDX) {
                int slot = vars_lookup(op->arridx.name);
                assert(slot >= 0 && "unknown array in address-of");
                Type *vt = var_slots[slot].type;
                TypeKind ek = vt->inner ? vt->inner->kind : TY_I32;
                int idx_reg = emit_expr(op->arridx.idx);
                idx_reg = coerce_index_i64(op->arridx.idx, idx_reg);
                int r = new_reg();
                if (vt->kind == TY_ARRAY) {
                    fprintf(out, "  %%t%d = getelementptr [%llu x ",
                            r, (unsigned long long)vt->array_size);
                    emit_llvm_type(vt->inner);
                    fprintf(out, "], ptr %%v%d, i32 0, i64 ", var_slots[slot].reg);
                    emit_ref(idx_reg); fprintf(out, "\n");
                } else if (var_slots[slot].direct_reg >= 0) {
                    fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                            r, llvm_scalar(ek), var_slots[slot].direct_reg);
                    emit_ref(idx_reg); fprintf(out, "\n");
                } else {
                    int loaded = new_reg();
                    fprintf(out, "  %%t%d = load ptr, ptr %%v%d\n", loaded, var_slots[slot].reg);
                    fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                            r, llvm_scalar(ek), loaded);
                    emit_ref(idx_reg); fprintf(out, "\n");
                }
                return r;
            }
            if (op->kind == EX_FIELD) {
                int slot = vars_lookup(op->field.var);
                assert(slot >= 0 && "unknown struct in address-of");
                const char *sname; int is_vreg;
                int base = struct_base_ref(slot, &sname, &is_vreg);
                int fi = cg_field_index(sname, op->field.field);
                assert(fi >= 0 && "unknown field in address-of");
                int r = new_reg();
                fprintf(out, "  %%t%d = getelementptr %%%s, ptr %s%d, i32 0, i32 %d\n",
                        r, sname, is_vreg ? "%v" : "%t", base, fi);
                return r;
            }
            fprintf(stderr, "address-of requires a variable, array element, or field\n");
            exit(1);
        }
        int or_reg = emit_expr(e->unop.operand);
        TypeKind ok = e->unop.operand->typ->kind;
        switch (e->unop.op) {
        case UOP_NEG: {
            if (IS_CONST(or_reg) && !const_table[CONST_IDX(or_reg)].is_float)
                return new_iconst(-const_table[CONST_IDX(or_reg)].ival);
            int r = new_reg();
            if (ok == TY_F32 || ok == TY_F64)
                fprintf(out, "  %%t%d = fneg %s %%t%d\n", r, llvm_scalar(ok), or_reg);
            else {
                fprintf(out, "  %%t%d = sub %s 0, ", r, llvm_scalar(ok));
                emit_ref(or_reg); fprintf(out, "\n");
            }
            return r;
        }
        case UOP_NOT: {
            int c1 = ensure_i1(or_reg, ok);
            if (IS_CONST(c1)) return new_iconst(!const_table[CONST_IDX(c1)].ival);
            int r = new_reg();
            fprintf(out, "  %%t%d = xor i1 %%t%d, true\n", r, c1);
            return r;
        }
        case UOP_BITNOT: {
            /* bitwise NOT = xor with all-ones (-1) */
            if (IS_CONST(or_reg)) return new_iconst(~const_table[CONST_IDX(or_reg)].ival);
            int r = new_reg();
            fprintf(out, "  %%t%d = xor %s ", r, llvm_scalar(ok));
            emit_ref(or_reg);
            fprintf(out, ", -1\n");
            return r;
        }
        case UOP_ADDROF:
            /* handled above, before the eager operand emit */
            break;
        case UOP_DEREF: {
            /* *ptr → load through the pointer value */
            int r = new_reg();
            TypeKind ek = e->typ->kind;
            fprintf(out, "  %%t%d = load %s, ptr %%t%d\n", r, llvm_scalar(ek), or_reg);
            return r;
        }
        }
        break;
    }

    case EX_CALL: {
        /* Capture tail-position flag BEFORE evaluating args (nested calls in
         * args are not in tail position). A `tail` hint lets LLVM's
         * TailCallElim turn self-tail-recursion into a loop — no stack growth,
         * no overflow on deep recursion, and faster. */
        int is_tail = g_tail; g_tail = 0;

        /* Evaluate all arguments */
        int arg_regs[64];
        assert(e->call.argc <= 64);
        for (size_t i = 0; i < e->call.argc; i++)
            arg_regs[i] = emit_expr(e->call.args[i]);

        /* `sys` builtin → native syscall via per-arch inline asm.
         * sys(num, a1..a6); each arg widened to i64. Source uses Linux x86-64
         * syscall numbers (Oh's ABI); the number is remapped per target.
         *   x86-64:  `syscall`  num=rax args=rdi,rsi,rdx,r10,r8,r9  ret=rax
         *   aarch64: `svc #0`   num=x8  args=x0,x1,x2,x3,x4,x5      ret=x0  */
        if (strcmp(e->call.name, "sys") == 0) {
            int arm = target_is_aarch64();
            /* arg-register constraints. x86: first slot is the number in rax
             * AND the return in rax. arm: number is a separate x8 input, args
             * x0..x5, return x0. */
            const char *x86regs[7]  = {"{ax}","{di}","{si}","{dx}","{r10}","{r8}","{r9}"};
            const char *armargs[6]  = {"{x0}","{x1}","{x2}","{x3}","{x4}","{x5}"};
            size_t n = e->call.argc; if (n > 7) n = 7;

            /* Remap a constant syscall number to the target's ABI. */
            if (arm && n >= 1 && IS_CONST(arg_regs[0])) {
                long mapped = remap_syscall_x86_to_arm64(const_table[CONST_IDX(arg_regs[0])].ival);
                arg_regs[0] = new_iconst(mapped);
            }

            /* widen each arg to i64 */
            int wide[7];
            for (size_t i = 0; i < n; i++) {
                TypeKind ak = e->call.args[i]->typ->kind;
                int w = new_reg();
                if (ak == TY_PTR || ak == TY_ARRAY) {
                    fprintf(out, "  %%t%d = ptrtoint ptr ", w); emit_ref(arg_regs[i]);
                    fprintf(out, " to i64\n");
                } else if (ak == TY_I64 || ak == TY_U64) {
                    fprintf(out, "  %%t%d = add i64 ", w); emit_ref(arg_regs[i]); fprintf(out, ", 0\n");
                } else {
                    fprintf(out, "  %%t%d = sext %s ", w, llvm_scalar(ak)); emit_ref(arg_regs[i]);
                    fprintf(out, " to i64\n");
                }
                wide[i] = w;
            }
            int r2 = new_reg();
            if (arm) {
                /* aarch64: x8=number, x0..x5=args, result in x0 */
                fprintf(out, "  %%t%d = call i64 asm sideeffect \"svc #0\", \"={x0},{x8}", r2);
                size_t nargs = n > 0 ? n - 1 : 0;            /* args after the number */
                for (size_t i = 0; i < nargs && i < 6; i++) fprintf(out, ",%s", armargs[i]);
                fprintf(out, ",~{memory}\"(");
                /* operand order: number first (x8), then args (x0..) */
                fprintf(out, "i64 %%t%d", wide[0]);
                for (size_t i = 1; i < n; i++) fprintf(out, ", i64 %%t%d", wide[i]);
                fprintf(out, ")\n");
                return r2;
            }
            /* x86-64: rax=number+return, rdi,rsi,... = args */
            fprintf(out, "  %%t%d = call i64 asm sideeffect \"syscall\", \"={ax}", r2);
            for (size_t i = 0; i < n; i++) fprintf(out, ",%s", x86regs[i]);
            fprintf(out, ",~{rcx},~{r11},~{memory}\"(");
            for (size_t i = 0; i < n; i++) {
                if (i) fprintf(out, ", ");
                fprintf(out, "i64 %%t%d", wide[i]);
            }
            fprintf(out, ")\n");
            return r2;
        }

        /* archid() → compile-time arch constant (0=x86_64, 1=aarch64). */
        if (strcmp(e->call.name,"archid")==0) {
            return new_iconst(target_is_aarch64() ? 1 : 0);
        }
        /* Bit intrinsics → single hardware instruction. */
        if (strcmp(e->call.name,"popcount")==0 || strcmp(e->call.name,"clz")==0 ||
            strcmp(e->call.name,"ctz")==0 || strcmp(e->call.name,"bswap")==0) {
            /* widen/narrow the single arg to i32 */
            int a0 = arg_regs[0];
            TypeKind ak = e->call.args[0]->typ->kind;
            int xi = a0;
            if (ak != TY_I32 && ak != TY_U32) {
                xi = new_reg();
                if (ak==TY_I64||ak==TY_U64)
                    { fprintf(out,"  %%t%d = trunc i64 ",xi); emit_ref(a0); fprintf(out," to i32\n"); }
                else
                    { fprintf(out,"  %%t%d = sext %s ",xi,llvm_scalar(ak)); emit_ref(a0); fprintf(out," to i32\n"); }
            }
            int r2 = new_reg();
            if (strcmp(e->call.name,"popcount")==0) {
                fprintf(out,"  %%t%d = call i32 @llvm.ctpop.i32(i32 ",r2); emit_ref(xi); fprintf(out,")\n");
            } else if (strcmp(e->call.name,"clz")==0) {
                fprintf(out,"  %%t%d = call i32 @llvm.ctlz.i32(i32 ",r2); emit_ref(xi); fprintf(out,", i1 0)\n");
            } else if (strcmp(e->call.name,"ctz")==0) {
                fprintf(out,"  %%t%d = call i32 @llvm.cttz.i32(i32 ",r2); emit_ref(xi); fprintf(out,", i1 0)\n");
            } else {
                fprintf(out,"  %%t%d = call i32 @llvm.bswap.i32(i32 ",r2); emit_ref(xi); fprintf(out,")\n");
            }
            return r2;
        }

        /* SIMD reduction builtins → call the emitted helper. Pointer/array
         * args are already in ptr registers; the count arg is i32. */
        if (strcmp(e->call.name,"vsum")==0 || strcmp(e->call.name,"dot")==0) {
            int is_dot = (strcmp(e->call.name,"dot")==0);
            int r2 = new_reg();
            if (is_dot) {
                fprintf(out, "  %%t%d = call fastcc i32 @__oh_dot(ptr ", r2);
                emit_ref(arg_regs[0]); fprintf(out, ", ptr "); emit_ref(arg_regs[1]);
                fprintf(out, ", i32 "); emit_ref(arg_regs[2]); fprintf(out, ")\n");
            } else {
                fprintf(out, "  %%t%d = call fastcc i32 @__oh_vsum(ptr ", r2);
                emit_ref(arg_regs[0]); fprintf(out, ", i32 "); emit_ref(arg_regs[1]);
                fprintf(out, ")\n");
            }
            return r2;
        }

        TypeKind rk = e->typ->kind;
        int r = (rk == TY_VOID) ? -1 : new_reg();

        /* All callees are internal fastcc functions (whole-program); the cc
         * must match the definition. fastcc enables clang's tail-call /
         * recursion-to-iteration transforms. */
        /* A `tail` call must NOT reference the caller's stack frame. If any
         * argument is a pointer/array, it may point into the caller's locals
         * (e.g. maxarr(arr,5) with a local arr) — tearing the frame down would
         * dangle it. Only tail-call when all args are scalar values. */
        if (is_tail) {
            for (size_t i = 0; i < e->call.argc; i++) {
                TypeKind ak = e->call.args[i]->typ->kind;
                if (ak == TY_PTR || ak == TY_ARRAY) { is_tail = 0; break; }
            }
        }
        const char *tailkw = is_tail ? "tail " : "";
        if (rk != TY_VOID)
            fprintf(out, "  %%t%d = %scall fastcc %s @%s(", r, tailkw, llvm_scalar(rk), e->call.name);
        else
            fprintf(out, "  %scall fastcc void @%s(", tailkw, e->call.name);

        for (size_t i = 0; i < e->call.argc; i++) {
            if (i > 0) fprintf(out, ", ");
            TypeKind ak = e->call.args[i]->typ->kind;
            fprintf(out, "%s ", llvm_scalar(ak));
            emit_ref(arg_regs[i]);
        }
        fprintf(out, ")\n");
        return r;
    }

    case EX_ARRAYLIT: {
        /* Array literals should only appear in vardecl initializers.
         * We should not reach here from emit_expr directly in normal use.
         * Return -1 as a sentinel. */
        return -1;
    }

    case EX_ARRAYIDX: {
        /* a%i  — index into array or pointer variable */
        int idx_reg = emit_expr(e->arridx.idx);
        idx_reg = coerce_index_i64(e->arridx.idx, idx_reg);
        TypeKind ek = e->typ->kind;

        int slot_idx = vars_lookup(e->arridx.name);
        assert(slot_idx >= 0);
        Type *vt = var_slots[slot_idx].type;

        int ptr_reg = new_reg();
        if (vt->kind == TY_ARRAY) {
            fprintf(out, "  %%t%d = getelementptr [%llu x ",
                    ptr_reg, (unsigned long long)vt->array_size);
            emit_llvm_type(vt->inner);
            fprintf(out, "], ptr %%v%d, i32 0, i64 ", var_slots[slot_idx].reg);
            emit_ref(idx_reg); fprintf(out, "\n");
        } else if (var_slots[slot_idx].direct_reg >= 0) {
            fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                    ptr_reg,
                    llvm_scalar(vt->inner ? vt->inner->kind : TY_I32),
                    var_slots[slot_idx].direct_reg);
            emit_ref(idx_reg); fprintf(out, "\n");
        } else {
            /* Pointer param: first load the pointer from its alloca slot,
             * then GEP to reach the element. */
            int loaded_ptr = new_reg();
            fprintf(out, "  %%t%d = load ptr, ptr %%v%d\n",
                    loaded_ptr, var_slots[slot_idx].reg);
            fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                    ptr_reg,
                    llvm_scalar(vt->inner ? vt->inner->kind : TY_I32),
                    loaded_ptr);
            emit_ref(idx_reg); fprintf(out, "\n");
        }

        int r = new_reg();
        fprintf(out, "  %%t%d = load %s, ptr %%t%d\n", r, llvm_scalar(ek), ptr_reg);
        return r;
    }

    case EX_STRLIT: {
        /* Return a pointer to the global string constant */
        int si = str_intern(e->sval);
        size_t len = str_decoded_len(e->sval) + 1; /* must match the global's size */
        int r = new_reg();
        fprintf(out, "  %%t%d = getelementptr [%zu x i8], ptr @.str_%d, i32 0, i32 0\n",
                r, len, si);
        return r;
    }

    case EX_CAST: {
        int or2 = emit_expr(e->cast.operand);
        TypeKind from_k = e->cast.operand->typ->kind;
        TypeKind to_k   = e->cast.to->kind;
        /* The cast branches reference the operand as %tN, so a constant operand
         * (e.g. `(6)5`, `(*3)0`, `(d)5`) must first be materialized into a real
         * register — otherwise we'd emit `%t<const-id>` (undefined value). */
        if (IS_CONST(or2)) {
            int m = new_reg();
            if (from_k == TY_F32 || from_k == TY_F64) {
                fprintf(out, "  %%t%d = fadd %s 0x0000000000000000, ", m, llvm_scalar(from_k));
                emit_ref(or2); fprintf(out, "\n");
            } else {
                fprintf(out, "  %%t%d = add %s 0, ", m, llvm_scalar(from_k));
                emit_ref(or2); fprintf(out, "\n");
            }
            or2 = m;
        }
        int r = new_reg();
        int from_f = (from_k == TY_F32 || from_k == TY_F64);
        int to_f   = (to_k   == TY_F32 || to_k   == TY_F64);
        int from_unsigned = (from_k==TY_U8||from_k==TY_U16||from_k==TY_U32||from_k==TY_U64);
        int to_unsigned   = (to_k==TY_U8||to_k==TY_U16||to_k==TY_U32||to_k==TY_U64);
        if (from_k == to_k) {
            /* Same type: trivial copy */
            if (to_k == TY_PTR)
                fprintf(out, "  %%t%d = getelementptr i8, ptr %%t%d, i32 0\n", r, or2);
            else if (to_f)
                return or2; /* float no-op cast — reuse the value (no `add float`) */
            else
                fprintf(out, "  %%t%d = add %s %%t%d, 0\n", r, llvm_scalar(to_k), or2);
        } else if (from_f && to_f) {
            /* f32 <-> f64 */
            if (from_k == TY_F32)
                fprintf(out, "  %%t%d = fpext float %%t%d to double\n", r, or2);
            else
                fprintf(out, "  %%t%d = fptrunc double %%t%d to float\n", r, or2);
        } else if (from_f && !to_f) {
            /* float -> int */
            fprintf(out, "  %%t%d = %s %s %%t%d to %s\n", r,
                    to_unsigned ? "fptoui" : "fptosi", llvm_scalar(from_k), or2, llvm_scalar(to_k));
        } else if (!from_f && to_f) {
            /* int -> float */
            fprintf(out, "  %%t%d = %s %s %%t%d to %s\n", r,
                    from_unsigned ? "uitofp" : "sitofp", llvm_scalar(from_k), or2, llvm_scalar(to_k));
        } else if (to_k == TY_PTR && (from_k == TY_I32 || from_k == TY_I64 || from_k == TY_U64)) {
            fprintf(out, "  %%t%d = inttoptr %s %%t%d to ptr\n", r, llvm_scalar(from_k), or2);
        } else if ((to_k == TY_I32 || to_k == TY_I64 || to_k == TY_U64) && from_k == TY_PTR) {
            fprintf(out, "  %%t%d = ptrtoint ptr %%t%d to %s\n", r, or2, llvm_scalar(to_k));
        } else {
            /* Integer narrowing/widening/sign */
            int from_bits = 0, to_bits = 0;
            switch(from_k) { case TY_I8: case TY_U8: from_bits=8; break; case TY_I16: case TY_U16: from_bits=16; break; case TY_I32: case TY_U32: from_bits=32; break; case TY_I64: case TY_U64: from_bits=64; break; default: from_bits=32; break; }
            switch(to_k)   { case TY_I8: case TY_U8: to_bits=8;   break; case TY_I16: case TY_U16: to_bits=16;   break; case TY_I32: case TY_U32: to_bits=32;   break; case TY_I64: case TY_U64: to_bits=64;   break; default: to_bits=32;   break; }
            if (to_bits < from_bits)
                fprintf(out, "  %%t%d = trunc %s %%t%d to %s\n", r, llvm_scalar(from_k), or2, llvm_scalar(to_k));
            else if (to_bits > from_bits) {
                bool from_signed = (from_k==TY_I8||from_k==TY_I16||from_k==TY_I32||from_k==TY_I64);
                if (from_signed)
                    fprintf(out, "  %%t%d = sext %s %%t%d to %s\n", r, llvm_scalar(from_k), or2, llvm_scalar(to_k));
                else
                    fprintf(out, "  %%t%d = zext %s %%t%d to %s\n", r, llvm_scalar(from_k), or2, llvm_scalar(to_k));
            } else {
                /* Same bit-width: bitcast */
                fprintf(out, "  %%t%d = bitcast %s %%t%d to %s\n", r, llvm_scalar(from_k), or2, llvm_scalar(to_k));
            }
        }
        return r;
    }

    case EX_TERNARY: {
        /* cond ? a : b
         * If BOTH branches are side-effect-free (no calls), emit a `select`
         * — LLVM lowers it to a branchless cmov and can vectorise it.
         * If either branch may have side effects or is expensive (a call),
         * lower to BRANCHES so the untaken branch is never evaluated. This is
         * mandatory for recursive ternaries like fib() where a select would
         * recurse forever. */
        int cr  = emit_expr(e->ternary.cond);
        int ci1 = ensure_i1(cr, e->ternary.cond->typ->kind);
        if (IS_CONST(ci1))
            return const_table[CONST_IDX(ci1)].ival
                 ? emit_expr(e->ternary.then_e)
                 : emit_expr(e->ternary.else_e);

        TypeKind tk = e->ternary.then_e->typ->kind;

        if (!expr_impure(e->ternary.then_e) && !expr_impure(e->ternary.else_e)) {
            /* Branchless select */
            int tr  = emit_expr(e->ternary.then_e);
            int er2 = emit_expr(e->ternary.else_e);
            int r = new_reg();
            fprintf(out, "  %%t%d = select i1 %%t%d, %s ", r, ci1, llvm_scalar(tk));
            emit_ref(tr); fprintf(out, ", %s ", llvm_scalar(tk)); emit_ref(er2);
            fprintf(out, "\n");
            return r;
        }

        /* Branch-based lowering (lazy evaluation of each arm) */
        int slot = new_reg();
        fprintf(out, "  %%v%d = alloca %s\n", slot, llvm_scalar(tk));
        int lt = new_label(), le = new_label(), lend = new_label();
        fprintf(out, "  br i1 %%t%d, label %%L%d, label %%L%d\n", ci1, lt, le);
        fprintf(out, "L%d:\n", lt);
        int tr = emit_expr(e->ternary.then_e);
        fprintf(out, "  store %s ", llvm_scalar(tk)); emit_ref(tr);
        fprintf(out, ", ptr %%v%d\n  br label %%L%d\n", slot, lend);
        fprintf(out, "L%d:\n", le);
        int er2 = emit_expr(e->ternary.else_e);
        fprintf(out, "  store %s ", llvm_scalar(tk)); emit_ref(er2);
        fprintf(out, ", ptr %%v%d\n  br label %%L%d\n", slot, lend);
        fprintf(out, "L%d:\n", lend);
        int r = new_reg();
        fprintf(out, "  %%t%d = load %s, ptr %%v%d\n", r, llvm_scalar(tk), slot);
        return r;
    }

    } /* switch */

    assert(0 && "unreachable emit_expr");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Store a value into an lvalue                                         */
/* ------------------------------------------------------------------ */

/* Store value register into the array element name%idx */
static void store_arrayidx(const char *name, Expr *idx_expr, int val_reg, TypeKind ek) {
    int idx_reg = emit_expr(idx_expr);
    idx_reg = coerce_index_i64(idx_expr, idx_reg);
    int slot_idx = vars_lookup(name);
    assert(slot_idx >= 0);
    Type *vt = var_slots[slot_idx].type;

    int ptr_reg = new_reg();
    if (vt->kind == TY_ARRAY) {
        fprintf(out, "  %%t%d = getelementptr [%llu x ",
                ptr_reg, (unsigned long long)vt->array_size);
        emit_llvm_type(vt->inner);
        fprintf(out, "], ptr %%v%d, i32 0, i64 %%t%d\n",
                var_slots[slot_idx].reg, idx_reg);
    } else {
        fprintf(out, "  %%t%d = getelementptr %s, ptr %%v%d, i64 %%t%d\n",
                ptr_reg, llvm_scalar(vt->inner ? vt->inner->kind : TY_I32),
                var_slots[slot_idx].reg, idx_reg);
    }
    fprintf(out, "  store %s ", llvm_scalar(ek)); emit_ref(val_reg);
    fprintf(out, ", ptr %%t%d\n", ptr_reg);
}

/* ------------------------------------------------------------------ */
/* Statement emission                                                  */
/* ------------------------------------------------------------------ */

static void emit_stmt(Stmt *s);

static void emit_stmts(Stmt **body, size_t n) {
    for (size_t i = 0; i < n; i++) emit_stmt(body[i]);
}

static void emit_stmt(Stmt *s) {
    switch (s->kind) {

    case ST_VARDECL: {
        Type *t = s->vardecl.typ;
        /* alloca already emitted in entry block by the hoist pass; just bind
         * the name to its pre-assigned slot and emit the initialiser store. */
        int slot = vars_alloc_with_reg(s->vardecl.name, t, s->vardecl.cg_reg);
        int vreg  = var_slots[slot].reg;

        if (t->kind == TY_ARRAY) {
            /* Initialise elements if array literal */
            if (s->vardecl.init && s->vardecl.init->kind == EX_ARRAYLIT) {
                Expr *al = s->vardecl.init;
                for (size_t i = 0; i < al->arrlit.elemc; i++) {
                    int ev = emit_expr(al->arrlit.elems[i]);
                    int ep = new_reg();
                    fprintf(out, "  %%t%d = getelementptr [%llu x ",
                            ep, (unsigned long long)t->array_size);
                    emit_llvm_type(t->inner);
                    fprintf(out, "], ptr %%v%d, i32 0, i32 %zu\n", vreg, i);
                    fprintf(out, "  store %s ", llvm_scalar(t->inner->kind));
                    emit_ref(ev); fprintf(out, ", ptr %%t%d\n", ep);
                }
            } else if (s->vardecl.init) {
                /* array fill: `name:[N]T = scalar`. Zero-fill uses a single
                 * zeroinitializer store (clang -> memset); non-zero uses a loop. */
                int fv = emit_expr(s->vardecl.init);
                if (IS_CONST(fv) && const_table[CONST_IDX(fv)].ival == 0) {
                    fprintf(out, "  store [%llu x ", (unsigned long long)t->array_size);
                    emit_llvm_type(t->inner);
                    fprintf(out, "] zeroinitializer, ptr %%v%d\n", vreg);
                } else {
                    int ci = new_reg();                 /* counter alloca */
                    fprintf(out, "  %%v%d = alloca i32\n  store i32 0, ptr %%v%d\n", ci, ci);
                    int lc = new_label(), lb = new_label(), le = new_label();
                    fprintf(out, "  br label %%L%d\nL%d:\n", lc, lc);
                    int c0 = new_reg();
                    fprintf(out, "  %%t%d = load i32, ptr %%v%d\n", c0, ci);
                    int cc = new_reg();
                    fprintf(out, "  %%t%d = icmp slt i32 %%t%d, %llu\n", cc, c0, (unsigned long long)t->array_size);
                    fprintf(out, "  br i1 %%t%d, label %%L%d, label %%L%d\nL%d:\n", cc, lb, le, lb);
                    int ep = new_reg();
                    fprintf(out, "  %%t%d = getelementptr [%llu x ", ep, (unsigned long long)t->array_size);
                    emit_llvm_type(t->inner);
                    fprintf(out, "], ptr %%v%d, i32 0, i32 %%t%d\n", vreg, c0);
                    fprintf(out, "  store %s ", llvm_scalar(t->inner->kind)); emit_ref(fv);
                    fprintf(out, ", ptr %%t%d\n", ep);
                    int c1 = new_reg();
                    fprintf(out, "  %%t%d = add i32 %%t%d, 1\n  store i32 %%t%d, ptr %%v%d\n", c1, c0, c1, ci);
                    fprintf(out, "  br label %%L%d\nL%d:\n", lc, le);
                }
            }
        } else {
            /* scalar: alloca in entry; just store the initialiser */
            if (s->vardecl.init) {
                int val = emit_expr(s->vardecl.init);
                fprintf(out, "  store %s ", llvm_scalar(t->kind));
                emit_ref(val); fprintf(out, ", ptr %%v%d\n", vreg);
            }
        }
        break;
    }

    case ST_ASSIGN: {
        int idx = vars_lookup(s->assign.name);
        assert(idx >= 0 && "unknown variable in assign");
        Type *vt = var_slots[idx].type;
        int val = emit_expr(s->assign.rhs);
        fprintf(out, "  store %s ", llvm_scalar(vt->kind));
        emit_ref(val); fprintf(out, ", ptr %%v%d\n", var_slots[idx].reg);
        break;
    }

    case ST_DEREFASSIGN: {
        /* *name = rhs — load the pointer from the var, then store */
        int idx = vars_lookup(s->derefassign.name);
        assert(idx >= 0);
        Type *vt = var_slots[idx].type; /* should be TY_PTR */
        int ptr_val = new_reg();
        if (var_slots[idx].direct_reg >= 0)
            ptr_val = var_slots[idx].direct_reg;
        else
            fprintf(out, "  %%t%d = load ptr, ptr %%v%d\n", ptr_val, var_slots[idx].reg);
        int rval = emit_expr(s->derefassign.rhs);
        TypeKind ek = vt->inner ? vt->inner->kind : TY_I32;
        fprintf(out, "  store %s ", llvm_scalar(ek));
        emit_ref(rval); fprintf(out, ", ptr %%t%d\n", ptr_val);
        break;
    }

    case ST_IDXASSIGN: {
        /* name%idx = rhs — store through array or pointer */
        int rval = emit_expr(s->idxassign.rhs);
        int slot2 = vars_lookup(s->idxassign.name);
        assert(slot2 >= 0 && "unknown var in idxassign");
        Type *vt2 = var_slots[slot2].type;
        TypeKind ek2 = vt2->inner ? vt2->inner->kind : TY_I32;
        int idx_reg = emit_expr(s->idxassign.idx);
        idx_reg = coerce_index_i64(s->idxassign.idx, idx_reg);
        int ptr_reg = new_reg();
        if (vt2->kind == TY_ARRAY) {
            fprintf(out, "  %%t%d = getelementptr [%llu x ",
                    ptr_reg, (unsigned long long)vt2->array_size);
            emit_llvm_type(vt2->inner);
            fprintf(out, "], ptr %%v%d, i32 0, i64 ", var_slots[slot2].reg);
            emit_ref(idx_reg); fprintf(out, "\n");
        } else if (var_slots[slot2].direct_reg >= 0) {
            fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                    ptr_reg, llvm_scalar(ek2), var_slots[slot2].direct_reg);
            emit_ref(idx_reg); fprintf(out, "\n");
        } else {
            int loaded = new_reg();
            fprintf(out, "  %%t%d = load ptr, ptr %%v%d\n", loaded, var_slots[slot2].reg);
            fprintf(out, "  %%t%d = getelementptr %s, ptr %%t%d, i64 ",
                    ptr_reg, llvm_scalar(ek2), loaded);
            emit_ref(idx_reg); fprintf(out, "\n");
        }
        fprintf(out, "  store %s ", llvm_scalar(ek2)); emit_ref(rval);
        fprintf(out, ", ptr %%t%d\n", ptr_reg);
        break;
    }

    case ST_FIELDASSIGN: {
        /* var.field = rhs — GEP to the field, store. var may be a local struct
         * value or a pointer-to-struct (auto-deref). */
        int rval = emit_expr(s->fieldassign.rhs);
        int slot = vars_lookup(s->fieldassign.var);
        assert(slot >= 0 && "unknown struct var in fieldassign");
        const char *sname; int is_vreg;
        int base = struct_base_ref(slot, &sname, &is_vreg);
        int fi = cg_field_index(sname, s->fieldassign.field);
        assert(fi >= 0 && "unknown struct field in fieldassign");
        int p = new_reg();
        fprintf(out, "  %%t%d = getelementptr %%%s, ptr %s%d, i32 0, i32 %d\n",
                p, sname, is_vreg ? "%v" : "%t", base, fi);
        StructDef *sd = cg_lookup_struct(sname);
        Type *ft = sd->field_types[fi];
        fprintf(out, "  store ");
        emit_llvm_type(ft);
        fprintf(out, " "); emit_ref(rval); fprintf(out, ", ptr %%t%d\n", p);
        break;
    }

    case ST_RETURN: {
        if (!s->ret.val) {
            fprintf(out, "  ret void\n");
            break;
        }
        /* `return cond ? a : b` — emit a direct `ret` in each arm rather than
         * storing to a slot and loading at a merge point. This matches C's
         * `if(cond) return a; return b;` shape, which clang's inliner unrolls
         * for recursive functions like fib (the slot+load form blocks it). */
        Expr *rv = s->ret.val;
        if (rv->kind == EX_TERNARY) {
            int cr  = emit_expr(rv->ternary.cond);
            int ci1 = ensure_i1(cr, rv->ternary.cond->typ->kind);
            if (IS_CONST(ci1)) {
                Expr *taken = const_table[CONST_IDX(ci1)].ival ? rv->ternary.then_e : rv->ternary.else_e;
                g_tail = (taken->kind == EX_CALL);
                int t = emit_expr(taken);
                fprintf(out, "  ret %s ", llvm_scalar(taken->typ->kind)); emit_ref(t); fprintf(out,"\n");
                break;
            }
            int lt = new_label(), le = new_label();
            fprintf(out, "  br i1 %%t%d, label %%L%d, label %%L%d\n", ci1, lt, le);
            fprintf(out, "L%d:\n", lt);
            g_tail = (rv->ternary.then_e->kind == EX_CALL);
            int tr = emit_expr(rv->ternary.then_e);
            fprintf(out, "  ret %s ", llvm_scalar(rv->ternary.then_e->typ->kind)); emit_ref(tr); fprintf(out,"\n");
            fprintf(out, "L%d:\n", le);
            g_tail = (rv->ternary.else_e->kind == EX_CALL);
            int er = emit_expr(rv->ternary.else_e);
            fprintf(out, "  ret %s ", llvm_scalar(rv->ternary.else_e->typ->kind)); emit_ref(er); fprintf(out,"\n");
            break;
        }
        g_tail = (rv->kind == EX_CALL);   /* tail call if return value is a direct call */
        int r = emit_expr(rv);
        fprintf(out, "  ret %s ", llvm_scalar(rv->typ->kind));
        emit_ref(r); fprintf(out, "\n");
        break;
    }

    case ST_IF: {
        int cond_reg = emit_expr(s->ifst.cond);
        /* Ensure i1 */
        int cond_i1 = ensure_i1(cond_reg, s->ifst.cond->typ->kind);

        int lbl_then = new_label();
        int lbl_else = new_label();
        int lbl_end  = new_label();

        if (s->ifst.else_body && s->ifst.else_len > 0) {
            fprintf(out, "  br i1 "); emit_ref(cond_i1);
            fprintf(out, ", label %%L%d, label %%L%d\n", lbl_then, lbl_else);
        } else {
            fprintf(out, "  br i1 "); emit_ref(cond_i1);
            fprintf(out, ", label %%L%d, label %%L%d\n", lbl_then, lbl_end);
        }

        /* then block */
        fprintf(out, "L%d:\n", lbl_then);
        scope_push(); emit_stmts(s->ifst.then_body, s->ifst.then_len); scope_pop();
        fprintf(out, "  br label %%L%d\n", lbl_end);

        /* else block */
        if (s->ifst.else_body && s->ifst.else_len > 0) {
            fprintf(out, "L%d:\n", lbl_else);
            scope_push(); emit_stmts(s->ifst.else_body, s->ifst.else_len); scope_pop();
            fprintf(out, "  br label %%L%d\n", lbl_end);
        }

        /* end block */
        fprintf(out, "L%d:\n", lbl_end);
        break;
    }

    case ST_FOR: {
        /*
         * {
         *   init;
         *   br Lcond
         * Lcond:
         *   %c = cond
         *   br %c Lbody Lend
         * Lbody:
         *   body...
         *   step
         *   br Lcond
         * Lend:
         * }
         */
        int lbl_cond = new_label();
        int lbl_body = new_label();
        int lbl_end  = new_label();

        /* For-loop init var gets its own scope covering cond/step/body */
        scope_push();

        /* Init */
        if (s->forst.has_init_decl) {
            /* alloca already emitted in entry by hoist pass */
            int slot = vars_alloc_with_reg(s->forst.init_name, s->forst.init_typ, s->forst.cg_reg);
            int vreg  = var_slots[slot].reg;
            int iv = emit_expr(s->forst.init_init);
            fprintf(out, "  store %s ", llvm_scalar(s->forst.init_typ->kind));
            emit_ref(iv); fprintf(out, ", ptr %%v%d\n", vreg);
        } else if (s->forst.has_init_assign) {
            int idx = vars_lookup(s->forst.init_name);
            assert(idx >= 0);
            Type *vt = var_slots[idx].type;
            int iv = emit_expr(s->forst.init_rhs);
            fprintf(out, "  store %s ", llvm_scalar(vt->kind));
            emit_ref(iv); fprintf(out, ", ptr %%v%d\n", var_slots[idx].reg);
        }

        fprintf(out, "  br label %%L%d\n", lbl_cond);
        fprintf(out, "L%d:\n", lbl_cond);

        int cond_r = emit_expr(s->forst.cond);
        int cond_i1 = ensure_i1(cond_r, s->forst.cond->typ->kind);
        fprintf(out, "  br i1 "); emit_ref(cond_i1);
        fprintf(out, ", label %%L%d, label %%L%d\n", lbl_body, lbl_end);

        /* Body gets its own inner scope */
        fprintf(out, "L%d:\n", lbl_body);
        scope_push(); emit_stmts(s->forst.body, s->forst.body_len); scope_pop();

        /* Step */
        if (s->forst.has_step_assign) {
            int idx = vars_lookup(s->forst.step_name);
            assert(idx >= 0);
            Type *vt = var_slots[idx].type;
            int sv = emit_expr(s->forst.step_rhs);
            fprintf(out, "  store %s ", llvm_scalar(vt->kind));
            emit_ref(sv); fprintf(out, ", ptr %%v%d\n", var_slots[idx].reg);
        } else if (s->forst.step_expr) {
            emit_expr(s->forst.step_expr); /* evaluate for side effects */
        }

        fprintf(out, "  br label %%L%d\n", lbl_cond);
        fprintf(out, "L%d:\n", lbl_end);
        scope_pop();  /* pop for-loop init scope */
        break;
    }

    case ST_WHILE: {
        int lbl_cond = new_label();
        int lbl_body = new_label();
        int lbl_end  = new_label();

        fprintf(out, "  br label %%L%d\n", lbl_cond);
        fprintf(out, "L%d:\n", lbl_cond);

        int cond_r  = emit_expr(s->whilest.cond);
        int cond_i1 = ensure_i1(cond_r, s->whilest.cond->typ->kind);
        fprintf(out, "  br i1 "); emit_ref(cond_i1);
        fprintf(out, ", label %%L%d, label %%L%d\n", lbl_body, lbl_end);

        fprintf(out, "L%d:\n", lbl_body);
        scope_push(); emit_stmts(s->whilest.body, s->whilest.body_len); scope_pop();
        fprintf(out, "  br label %%L%d\n", lbl_cond);

        fprintf(out, "L%d:\n", lbl_end);
        break;
    }

    case ST_EXPRSTMT:
        if (s->exprstmt) emit_expr(s->exprstmt);  /* NULL = empty-block/blank-line no-op */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Function emission                                                   */
/* ------------------------------------------------------------------ */

/* ---- Alloca hoisting ----
 * LLVM requires (for good optimization) that all allocas live in the entry
 * block. Emitting an alloca inside a loop body defeats mem2reg promotion and
 * blocks idiom recognition (e.g. popcount loops). This pass walks the body
 * and emits one alloca per variable declaration / for-init into the entry
 * block, recording the assigned register in the AST node's cg_reg field. */
static void hoist_allocas(Stmt **body, size_t n);

static void hoist_one(Type *t, int *out_reg) {
    int reg = reg_ctr++;
    *out_reg = reg;
    if (t->kind == TY_ARRAY) {
        fprintf(out, "  %%v%d = alloca [%llu x ", reg, (unsigned long long)t->array_size);
        emit_llvm_type(t->inner);
        fprintf(out, "]\n");
    } else if (t->kind == TY_STRUCT) {
        fprintf(out, "  %%v%d = alloca %%%s\n", reg, t->struct_name);
    } else {
        fprintf(out, "  %%v%d = alloca %s\n", reg, llvm_scalar(t->kind));
    }
}

static void hoist_allocas(Stmt **body, size_t n) {
    for (size_t i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        switch (s->kind) {
        case ST_VARDECL:
            hoist_one(s->vardecl.typ, &s->vardecl.cg_reg);
            break;
        case ST_IF:
            hoist_allocas(s->ifst.then_body, s->ifst.then_len);
            hoist_allocas(s->ifst.else_body, s->ifst.else_len);
            break;
        case ST_FOR:
            if (s->forst.has_init_decl)
                hoist_one(s->forst.init_typ, &s->forst.cg_reg);
            hoist_allocas(s->forst.body, s->forst.body_len);
            break;
        case ST_WHILE:
            hoist_allocas(s->whilest.body, s->whilest.body_len);
            break;
        default: break;
        }
    }
}

static void emit_func(FuncDef *f) {
    /* Reset per-function state */
    reg_ctr = 0;
    vars_reset();
    const_count = 0;  /* reset inline-constant table */

    /* Determine return type */
    const char *ret_ll = llvm_scalar(f->ret_type->kind);

    /* Determine function attributes:
     *   nounwind — no C++ exceptions, enables stack frame elision
     *   nosync   — no synchronisation, enables loop transforms
     *   nofree   — never calls free(), enables alloca->register promotion
     *   readnone — pure function: return depends only on args (no ptr params) */
    bool has_ptr_param = false;
    for (size_t i = 0; i < f->param_count; i++) {
        TypeKind pk = f->param_types[i]->kind;
        if (pk == TY_PTR || pk == TY_ARRAY) { has_ptr_param = true; break; }
    }

    /* Function signature + attributes */
    /* Whole-program compilation: every function except `main` has internal
     * linkage so clang can clone, specialise and unroll it freely (this is
     * what makes C's `static fib` get unrolled). `internal` + the attributes
     * below give clang the same latitude it has with C static functions. */
    int is_main = (strcmp(f->name, "main") == 0);
    /* noalias (restrict) — sound subset: if a function has EXACTLY ONE pointer
     * or array parameter and its element type is a non-pointer scalar, that
     * pointer cannot alias any other pointer the function accesses (Oh has no
     * globals; locals are distinct storage). Marking it noalias lets LLVM
     * vectorize array kernels the way C does with `restrict`. Functions with
     * 2+ pointer params are NOT marked — a caller may legally pass aliasing
     * buffers, so assuming otherwise would be unsound. */
    int ptr_param_count = 0, sole_ptr_idx = -1;
    for (size_t i = 0; i < f->param_count; i++) {
        TypeKind pk = f->param_types[i]->kind;
        if (pk == TY_PTR || pk == TY_ARRAY) { ptr_param_count++; sole_ptr_idx = (int)i; }
    }
    int noalias_idx = -1;
    if (ptr_param_count == 1) {
        Type *pt = f->param_types[sole_ptr_idx];
        TypeKind ek = pt->inner ? pt->inner->kind : TY_VOID;
        switch (ek) {
        case TY_I8: case TY_I16: case TY_I32: case TY_I64:
        case TY_U8: case TY_U16: case TY_U32: case TY_U64:
        case TY_F32: case TY_F64:
            noalias_idx = sole_ptr_idx; break;
        default: break;
        }
    }
    fprintf(out, "define %s%s @%s(", is_main ? "" : "internal fastcc ", ret_ll, f->name);
    for (size_t i = 0; i < f->param_count; i++) {
        if (i > 0) fprintf(out, ", ");
        TypeKind pk = f->param_types[i]->kind;
        if (pk == TY_ARRAY) pk = TY_PTR;
        if ((int)i == noalias_idx)
            fprintf(out, "%s noalias %%p%zu", llvm_scalar(pk), i);
        else
            fprintf(out, "%s %%p%zu", llvm_scalar(pk), i);
    }
    fprintf(out, ")%s nounwind nosync nofree", is_main ? "" : " unnamed_addr");
    /* Interprocedural purity: pure functions (incl. recursive ones that only
     * call other pure functions, like fib) get memory(none) so clang can
     * optimise their call trees the way it does for equivalent C. */
    if (func_is_pure(f->name))
        fprintf(out, " memory(none)");
    /* Provably-terminating functions: willreturn+mustprogress lets LLVM turn
     * recursion into iteration (the fib gap). Never applied to functions with
     * unbounded loops or blocking syscalls. */
    if (func_is_wret(f->name))
        fprintf(out, " willreturn mustprogress");
    (void)has_ptr_param;
    fprintf(out, " {\nentry:\n");

    /*
     * Alloca slots for parameters.
     *
     * Optimisation: if a parameter is never assigned to in the function body
     * we can use it directly as an SSA value rather than spilling it to the
     * stack.  This removes the alloca+store+load chain for every read-only
     * parameter and shrinks the stack frame accordingly.
     *
     * We pre-allocate a block of "param registers" (%t0 … %t<param_count-1>)
     * that map to the incoming %p0…%p<N-1> values.  For mutated params we
     * still emit the traditional alloca path.
     */
    /* Reserve a contiguous block of SSA registers for param values */
    int param_base_reg = reg_ctr;
    reg_ctr += (int)f->param_count;

    for (size_t i = 0; i < f->param_count; i++) {
        Type *pt = f->param_types[i];
        TypeKind pk = pt->kind;
        if (pk == TY_ARRAY) pk = TY_PTR; /* treat as ptr at call boundary */

        int param_reg = param_base_reg + (int)i;

        /* Emit a trivial copy so the param has a %t<N> name in SSA form */
        if (pk == TY_PTR || pk == TY_ARRAY) {
            fprintf(out, "  %%t%d = getelementptr i8, ptr %%p%zu, i32 0\n",
                    param_reg, i);
        } else {
            /* For scalars: add 0 to get a fresh SSA copy */
            fprintf(out, "  %%t%d = add %s %%p%zu, 0\n",
                    param_reg, llvm_scalar(pk), i);
        }

        if (!param_is_mutated(f->param_names[i], f)) {
            /* Read-only param: record it as SSA-direct — no alloca needed */
            vars_alloc_direct(f->param_names[i], pt, param_reg);
        } else {
            /* Mutated param: spill to stack as before */
            int slot = vars_alloc(f->param_names[i], pt);
            int vreg  = var_slots[slot].reg;
            fprintf(out, "  %%v%d = alloca %s\n", vreg, llvm_scalar(pk));
            fprintf(out, "  store %s %%t%d, ptr %%v%d\n",
                    llvm_scalar(pk), param_reg, vreg);
        }
    }

    /* Hoist all body-declared allocas into the entry block (before any
     * branch) so mem2reg can promote them and loop idioms are recognised. */
    hoist_allocas(f->body, f->body_len);

    /* Body */
    emit_stmts(f->body, f->body_len);

    /* Fallthrough terminator. For void functions the body may legitimately
     * fall off the end (e.g. a trailing for-loop) — emit `ret void`. Using
     * `unreachable` there would tell LLVM the loop never exits, miscompiling
     * into an infinite loop. Non-void functions that fall off are a real
     * error, so `unreachable` is correct (and never reached in valid code). */
    if (f->ret_type->kind == TY_VOID)
        fprintf(out, "  ret void\n");
    else
        fprintf(out, "  unreachable\n");
    fprintf(out, "}\n\n");
}

/* ------------------------------------------------------------------ */
/* Top-level                                                           */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Target helpers                                                      */
/* ------------------------------------------------------------------ */

int target_parse(const char *s, TargetTriple *out_t) {
    if (strcmp(s, "x86_64-linux")   == 0) { *out_t = TARGET_X86_64_LINUX;   return 1; }
    if (strcmp(s, "x86_64-macos")   == 0) { *out_t = TARGET_X86_64_MACOS;   return 1; }
    if (strcmp(s, "x86_64-windows") == 0) { *out_t = TARGET_X86_64_WINDOWS; return 1; }
    if (strcmp(s, "aarch64-linux")  == 0) { *out_t = TARGET_AARCH64_LINUX;  return 1; }
    if (strcmp(s, "aarch64-macos")  == 0) { *out_t = TARGET_AARCH64_MACOS;  return 1; }
    return 0;
}

const char *target_llvm_triple(TargetTriple t) {
    switch (t) {
    case TARGET_X86_64_LINUX:   return "x86_64-unknown-linux-gnu";
    case TARGET_X86_64_MACOS:   return "x86_64-apple-macosx12.0";
    case TARGET_X86_64_WINDOWS: return "x86_64-pc-windows-msvc";
    case TARGET_AARCH64_LINUX:  return "aarch64-unknown-linux-gnu";
    case TARGET_AARCH64_MACOS:  return "aarch64-apple-macosx12.0";
    }
    return "x86_64-unknown-linux-gnu";
}

const char *target_datalayout(TargetTriple t) {
    switch (t) {
    case TARGET_X86_64_LINUX:
        /* Standard System V AMD64 ABI layout */
        return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
    case TARGET_X86_64_MACOS:
        /* macOS x86-64 uses the same layout as Linux x86-64 */
        return "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
    case TARGET_X86_64_WINDOWS:
        /* Windows uses COFF object format; pointer alignment differs slightly */
        return "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
    case TARGET_AARCH64_LINUX:
        /* AArch64 Linux (little-endian) */
        return "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128";
    case TARGET_AARCH64_MACOS:
        /* Apple Silicon (AArch64, Mach-O) */
        return "e-m:o-i64:64-i128:128-n32:64-S128";
    }
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
}

void codegen(Program *prog, const char *out_ll_path, TargetTriple target) {
    out = fopen(out_ll_path, "w");
    if (!out) {
        fprintf(stderr, "Cannot open output file %s\n", out_ll_path);
        exit(1);
    }

    g_target = target;
    lbl_ctr = 0;
    str_count = 0; /* reset string table for this compilation unit */
    cg_structs = prog->structs;
    cg_struct_count = prog->struct_count;
    compute_purity(prog);      /* interprocedural purity for memory(none) */
    compute_willreturn(prog);  /* termination analysis for willreturn/mustprogress */

    /* Collect all string literals from the whole program first */
    for (size_t i = 0; i < prog->func_count; i++) {
        FuncDef *f = &prog->funcs[i];
        collect_strings_stmts(f->body, f->body_len);
    }

    /* Module header */
    fprintf(out, "; Oh LLVM IR\n");
    fprintf(out, "target datalayout = \"%s\"\n", target_datalayout(target));
    fprintf(out, "target triple = \"%s\"\n\n", target_llvm_triple(target));

    /* Emit struct type definitions: %Name = type { field-types... } */
    for (size_t i = 0; i < prog->struct_count; i++) {
        StructDef *sd = &prog->structs[i];
        fprintf(out, "%%%s = type { ", sd->name);
        for (size_t j = 0; j < sd->field_count; j++) {
            if (j > 0) fprintf(out, ", ");
            emit_llvm_type(sd->field_types[j]);
        }
        fprintf(out, " }\n");
    }
    if (prog->struct_count) fprintf(out, "\n");

    /* Emit string literal globals (.rodata) */
    emit_string_globals();

    /* Hardware bit intrinsics exposed as Oh builtins. Declaring them
     * lets a single call lower to one instruction (popcnt/lzcnt/tzcnt/bswap)
     * instead of relying on clang to recognise a hand-written loop idiom. */
    fprintf(out, "declare i32 @llvm.ctpop.i32(i32)\n");
    fprintf(out, "declare i32 @llvm.ctlz.i32(i32, i1)\n");
    fprintf(out, "declare i32 @llvm.cttz.i32(i32, i1)\n");
    fprintf(out, "declare i32 @llvm.bswap.i32(i32)\n\n");

    /* SIMD reduction builtins, lowered to self-contained helper functions whose
     * tight loops clang -O3 vectorizes (mem-free phi loops). Emitted only when
     * used. Read-only pointers marked noalias so dot's two pointers vectorize. */
    g_uses_vsum = g_uses_dot = 0;
    for (size_t i = 0; i < prog->func_count; i++)
        scan_simd_stmts(prog->funcs[i].body, prog->funcs[i].body_len);
    if (g_uses_vsum) {
        fprintf(out,
            "define internal fastcc i32 @__oh_vsum(ptr noalias readonly %%a, i32 %%n) unnamed_addr nounwind nosync nofree willreturn mustprogress {\n"
            "entry:\n"
            "  %%pos = icmp sgt i32 %%n, 0\n"
            "  br i1 %%pos, label %%loop, label %%zero\n"
            "zero:\n"
            "  ret i32 0\n"
            "loop:\n"
            "  %%i = phi i32 [ 0, %%entry ], [ %%i1, %%loop ]\n"
            "  %%acc = phi i32 [ 0, %%entry ], [ %%acc1, %%loop ]\n"
            "  %%ep = getelementptr i32, ptr %%a, i32 %%i\n"
            "  %%v = load i32, ptr %%ep\n"
            "  %%acc1 = add nsw i32 %%acc, %%v\n"
            "  %%i1 = add nsw i32 %%i, 1\n"
            "  %%more = icmp slt i32 %%i1, %%n\n"
            "  br i1 %%more, label %%loop, label %%done\n"
            "done:\n"
            "  ret i32 %%acc1\n"
            "}\n\n");
    }
    if (g_uses_dot) {
        fprintf(out,
            "define internal fastcc i32 @__oh_dot(ptr noalias readonly %%a, ptr noalias readonly %%b, i32 %%n) unnamed_addr nounwind nosync nofree willreturn mustprogress {\n"
            "entry:\n"
            "  %%pos = icmp sgt i32 %%n, 0\n"
            "  br i1 %%pos, label %%loop, label %%zero\n"
            "zero:\n"
            "  ret i32 0\n"
            "loop:\n"
            "  %%i = phi i32 [ 0, %%entry ], [ %%i1, %%loop ]\n"
            "  %%acc = phi i32 [ 0, %%entry ], [ %%acc1, %%loop ]\n"
            "  %%ea = getelementptr i32, ptr %%a, i32 %%i\n"
            "  %%va = load i32, ptr %%ea\n"
            "  %%eb = getelementptr i32, ptr %%b, i32 %%i\n"
            "  %%vb = load i32, ptr %%eb\n"
            "  %%m = mul nsw i32 %%va, %%vb\n"
            "  %%acc1 = add nsw i32 %%acc, %%m\n"
            "  %%i1 = add nsw i32 %%i, 1\n"
            "  %%more = icmp slt i32 %%i1, %%n\n"
            "  br i1 %%more, label %%loop, label %%done\n"
            "done:\n"
            "  ret i32 %%acc1\n"
            "}\n\n");
    }

    /* Forward-declare all functions */
    for (size_t i = 0; i < prog->func_count; i++) {
        FuncDef *f = &prog->funcs[i];
        fprintf(out, "declare %s @%s_fwd(", llvm_scalar(f->ret_type->kind), f->name);
        for (size_t j = 0; j < f->param_count; j++) {
            if (j > 0) fprintf(out, ", ");
            TypeKind pk = f->param_types[j]->kind;
            if (pk == TY_ARRAY) pk = TY_PTR;
            fprintf(out, "%s", llvm_scalar(pk));
        }
        fprintf(out, ")\n");
    }
    fprintf(out, "\n");

    /* Emit each function */
    for (size_t i = 0; i < prog->func_count; i++) {
        emit_func(&prog->funcs[i]);
    }

    fclose(out);
    out = NULL;
}
