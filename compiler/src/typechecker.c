#include "typechecker.h"

// ---- Type helpers ----
static bool types_equal(Type *a, Type *b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TY_PTR || a->kind == TY_ARRAY) {
        if (a->kind == TY_ARRAY && a->array_size != b->array_size) return false;
        return types_equal(a->inner, b->inner);
    }
    return true;
}

static Type *make_type(Arena *a, TypeKind k) {
    Type *t = (Type*)arena_alloc(a, sizeof(Type));
    t->kind = k;
    return t;
}

static bool is_int_kind(TypeKind k) {
    return k==TY_I8||k==TY_I16||k==TY_I32||k==TY_I64||
           k==TY_U8||k==TY_U16||k==TY_U32||k==TY_U64;
}

/* Integer literals are polymorphic constants: a literal (or negation/bitnot of
 * one) adopts whatever integer type it is used with, like C. Returns true if
 * `e` is such a constant-int expression and retypes it (and children) to `k`. */
static bool relit(Expr *e, TypeKind k, Arena *a) {
    if (!e) return false;
    if (e->kind == EX_INTLIT) { e->typ = make_type(a, k); return true; }
    if (e->kind == EX_UNOP && (e->unop.op==UOP_NEG || e->unop.op==UOP_BITNOT)) {
        if (relit(e->unop.operand, k, a)) { e->typ = make_type(a, k); return true; }
    }
    return false;
}

/* Float-literal polymorphism: a float literal (or its negation) adopts the
 * float type it is used with — so `x:f=1.5` types 1.5 as f32, not f64. */
static bool relit_float(Expr *e, TypeKind k, Arena *a) {
    if (!e || (k != TY_F32 && k != TY_F64)) return false;
    if (e->kind == EX_FLOATLIT) { e->typ = make_type(a, k); return true; }
    if (e->kind == EX_UNOP && e->unop.op==UOP_NEG) {
        if (relit_float(e->unop.operand, k, a)) { e->typ = make_type(a, k); return true; }
    }
    return false;
}

/* If exactly one side of a binary op is a constant int literal and the other is
 * a concrete int type, coerce the literal to that type. Mutates the exprs' typ. */
static void coerce_int_lits(Expr *lhs, Type **lt, Expr *rhs, Type **rt, Arena *a) {
    if (!is_int_kind((*lt)->kind) || !is_int_kind((*rt)->kind)) return;
    if ((*lt)->kind == (*rt)->kind) return;
    if (relit(rhs, (*lt)->kind, a)) { *rt = rhs->typ; return; }
    if (relit(lhs, (*rt)->kind, a)) { *lt = lhs->typ; return; }
}

static int int_width(TypeKind k) {
    switch (k) {
    case TY_I8: case TY_U8:   return 8;
    case TY_I16: case TY_U16: return 16;
    case TY_I32: case TY_U32: return 32;
    case TY_I64: case TY_U64: return 64;
    default: return 0;
    }
}
static Type *make_type(Arena *a, TypeKind k);
static Type *clone_type(Arena *a, Type *t);
/* Implicit widening: if *slot is a narrower int than `target`, wrap it in an
 * EX_CAST to target (codegen sext/zext). Never narrows (that needs an explicit
 * cast). Lets mixed-width code (i64 heap addrs + i32 counts) drop casts. */
static void widen(Expr **slot, Type *target, Arena *a) {
    Expr *e = *slot;
    if (!e || !e->typ) return;
    if (!is_int_kind(e->typ->kind) || !is_int_kind(target->kind)) return;
    if (int_width(e->typ->kind) >= int_width(target->kind)) return;
    Expr *c = (Expr*)arena_alloc(a, sizeof(Expr));
    memset(c, 0, sizeof(*c));
    c->kind = EX_CAST;
    c->cast.to = clone_type(a, target);
    c->cast.operand = e;
    c->typ = clone_type(a, target);
    *slot = c;
}
/* In a binary op, widen the narrower int operand to match the wider one. */
static void widen_binop(Expr *e, Type **lt, Type **rt, Arena *a) {
    if (!is_int_kind((*lt)->kind) || !is_int_kind((*rt)->kind)) return;
    int lw = int_width((*lt)->kind), rw = int_width((*rt)->kind);
    if (lw == rw) return;
    if (lw < rw) { widen(&e->binop.lhs, *rt, a); *lt = e->binop.lhs->typ; }
    else         { widen(&e->binop.rhs, *lt, a); *rt = e->binop.rhs->typ; }
}

static Type *clone_type(Arena *a, Type *t) {
    if (!t) return NULL;
    Type *c = (Type*)arena_alloc(a, sizeof(Type));
    *c = *t;
    if (t->inner) c->inner = clone_type(a, t->inner);
    return c;
}

// ---- Scope/variable tracking ----
#define MAX_SCOPES 64
#define MAX_VARS_PER_SCOPE 256

typedef struct {
    char *name;
    Type *typ;
} VarEntry;

typedef struct {
    VarEntry vars[MAX_VARS_PER_SCOPE];
    size_t   count;
} Scope;

typedef struct {
    Scope  scopes[MAX_SCOPES];
    size_t depth;
} ScopeStack;

typedef struct {
    char  *name;
    Type **param_types;
    size_t param_count;
    Type  *ret_type;
} FuncSig;

#define MAX_FUNCS 256
static FuncSig func_table[MAX_FUNCS];
static size_t  func_count = 0;

/* Struct registry (set up in typecheck() before checking bodies) */
static StructDef *g_structs = NULL;
static size_t     g_struct_count = 0;
static StructDef *lookup_struct(const char *name) {
    for (size_t i = 0; i < g_struct_count; i++)
        if (strcmp(g_structs[i].name, name) == 0) return &g_structs[i];
    return NULL;
}
static Type *struct_field_type(const char *sname, const char *fname) {
    StructDef *sd = lookup_struct(sname);
    if (!sd) { fprintf(stderr, "Unknown struct '%s'\n", sname); exit(1); }
    for (size_t i = 0; i < sd->field_count; i++)
        if (strcmp(sd->field_names[i], fname) == 0) return sd->field_types[i];
    fprintf(stderr, "Struct '%s' has no field '%s'\n", sname, fname); exit(1);
}

static void push_scope(ScopeStack *ss) {
    if (ss->depth >= MAX_SCOPES) { fprintf(stderr, "Scope overflow\n"); exit(1); }
    ss->scopes[ss->depth].count = 0;
    ss->depth++;
}
static void pop_scope(ScopeStack *ss) {
    if (ss->depth == 0) { fprintf(stderr, "Scope underflow\n"); exit(1); }
    ss->depth--;
}
static void declare_var(ScopeStack *ss, char *name, Type *typ) {
    Scope *sc = &ss->scopes[ss->depth - 1];
    if (sc->count >= MAX_VARS_PER_SCOPE) { fprintf(stderr, "Too many vars\n"); exit(1); }
    sc->vars[sc->count].name = name;
    sc->vars[sc->count].typ = typ;
    sc->count++;
}
static Type *lookup_var(ScopeStack *ss, char *name) {
    for (int i = (int)ss->depth - 1; i >= 0; i--) {
        Scope *sc = &ss->scopes[i];
        for (size_t j = 0; j < sc->count; j++) {
            if (strcmp(sc->vars[j].name, name) == 0) return sc->vars[j].typ;
        }
    }
    return NULL;
}
static FuncSig *lookup_func(char *name) {
    for (size_t i = 0; i < func_count; i++) {
        if (strcmp(func_table[i].name, name) == 0) return &func_table[i];
    }
    return NULL;
}

// ---- Infer expression type ----
static Type *infer(Expr *e, ScopeStack *ss, Arena *a) {
    switch (e->kind) {
    case EX_INTLIT: {
        Type *t = make_type(a, TY_I32);
        e->typ = t;
        return t;
    }
    case EX_FLOATLIT: {
        Type *t = make_type(a, TY_F64);
        e->typ = t;
        return t;
    }
    case EX_BOOLLIT: {
        Type *t = make_type(a, TY_BOOL);
        e->typ = t;
        return t;
    }
    case EX_IDENT: {
        Type *t = lookup_var(ss, e->ident);
        if (!t) { fprintf(stderr, "Undeclared variable '%s'\n", e->ident); exit(1); }
        e->typ = clone_type(a, t);
        return e->typ;
    }
    case EX_FIELD: {
        Type *vt = lookup_var(ss, e->field.var);
        if (!vt) { fprintf(stderr, "Undeclared variable '%s'\n", e->field.var); exit(1); }
        /* `.` works on a struct value or a pointer-to-struct (auto-deref). */
        const char *sn = NULL;
        if (vt->kind == TY_STRUCT) sn = vt->struct_name;
        else if (vt->kind == TY_PTR && vt->inner && vt->inner->kind == TY_STRUCT) sn = vt->inner->struct_name;
        else { fprintf(stderr, "'%s' is not a struct\n", e->field.var); exit(1); }
        e->typ = clone_type(a, struct_field_type(sn, e->field.field));
        return e->typ;
    }
    case EX_BINOP: {
        Type *lt = infer(e->binop.lhs, ss, a);
        Type *rt = infer(e->binop.rhs, ss, a);
        /* polymorphic int literals adopt the other operand's int type */
        coerce_int_lits(e->binop.lhs, &lt, e->binop.rhs, &rt, a);
        /* implicit widening: narrower int operand widens to the wider one
         * (skip for pointer arithmetic, handled below) */
        if (lt->kind != TY_PTR && rt->kind != TY_PTR)
            widen_binop(e, &lt, &rt, a);
        switch (e->binop.op) {
        case OP_ADD: case OP_SUB:
            /* Pointer arithmetic: ^T +/- i32 => ^T */
            if (lt->kind == TY_PTR &&
                (rt->kind == TY_I32 || rt->kind == TY_I64 ||
                 rt->kind == TY_U32 || rt->kind == TY_U64 ||
                 rt->kind == TY_I8  || rt->kind == TY_I16)) {
                e->typ = clone_type(a, lt);
                return e->typ;
            }
            if (!types_equal(lt, rt)) {
                fprintf(stderr, "Type mismatch in binary op\n"); exit(1);
            }
            e->typ = clone_type(a, lt);
            return e->typ;
        case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_BITOR: case OP_BITAND: case OP_XOR: case OP_LSHIFT: case OP_RSHIFT:
            if (!types_equal(lt, rt)) {
                fprintf(stderr, "Type mismatch in binary op\n"); exit(1);
            }
            e->typ = clone_type(a, lt);
            return e->typ;
        case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LTEQ: case OP_GTEQ:
            /* Allow pointer comparison too */
            if (lt->kind == TY_PTR && rt->kind == TY_PTR) {
                e->typ = make_type(a, TY_BOOL);
                return e->typ;
            }
            if (!types_equal(lt, rt)) {
                fprintf(stderr, "Type mismatch in comparison\n"); exit(1);
            }
            e->typ = make_type(a, TY_BOOL);
            return e->typ;
        case OP_AND: case OP_OR:
            e->typ = make_type(a, TY_BOOL);
            return e->typ;
        }
        break;
    }
    case EX_UNOP: {
        Type *ot = infer(e->unop.operand, ss, a);
        switch (e->unop.op) {
        case UOP_NEG:
            e->typ = clone_type(a, ot);
            return e->typ;
        case UOP_NOT:
            e->typ = make_type(a, TY_BOOL);
            return e->typ;
        case UOP_ADDROF: {
            Type *pt = make_type(a, TY_PTR);
            pt->inner = clone_type(a, ot);
            e->typ = pt;
            return e->typ;
        }
        case UOP_DEREF:
            /* `!` is deref on a pointer, but logical-NOT on a non-pointer
             * (bool/int) — reinterpret so `!p` derefs and `!b` negates. */
            if (ot->kind != TY_PTR) {
                e->unop.op = UOP_NOT;
                e->typ = make_type(a, TY_BOOL);
                return e->typ;
            }
            e->typ = clone_type(a, ot->inner);
            return e->typ;
        case UOP_BITNOT:
            e->typ = clone_type(a, ot);  /* bitwise NOT preserves type */
            return e->typ;
        }
        break;
    }
    case EX_TERNARY: {
        Type *ct = infer(e->ternary.cond,   ss, a);
        Type *tt = infer(e->ternary.then_e, ss, a);
        Type *et = infer(e->ternary.else_e, ss, a);
        (void)ct;
        if (!types_equal(tt, et)) {
            fprintf(stderr, "Ternary branches have different types\n"); exit(1);
        }
        e->typ = clone_type(a, tt);
        return e->typ;
    }
    case EX_CALL: {
        /* `sys` builtin: raw syscall. Variadic, any arg types, returns i64.
         * sys(num, a1..a6) — emits x86-64 `syscall` inline asm in codegen. */
        if (strcmp(e->call.name, "sys") == 0) {
            for (size_t i = 0; i < e->call.argc; i++) infer(e->call.args[i], ss, a);
            e->typ = make_type(a, TY_I64);
            return e->typ;
        }
        /* Bit intrinsics: popcount/clz/ctz/bswap — 1 int arg, return i32. */
        if (strcmp(e->call.name,"popcount")==0 || strcmp(e->call.name,"clz")==0 ||
            strcmp(e->call.name,"ctz")==0 || strcmp(e->call.name,"bswap")==0) {
            for (size_t i = 0; i < e->call.argc; i++) infer(e->call.args[i], ss, a);
            e->typ = make_type(a, TY_I32);
            return e->typ;
        }
        /* SIMD reduction builtins over i32 arrays/pointers:
         *   vsum(*a, n)     -> sum of a[0..n)              : i32
         *   dot(*a, *b, n)  -> sum of a[i]*b[i] for i<n    : i32
         * Lowered to a tight reduction loop that clang -O3 vectorizes. */
        if (strcmp(e->call.name,"vsum")==0 || strcmp(e->call.name,"dot")==0) {
            for (size_t i = 0; i < e->call.argc; i++) infer(e->call.args[i], ss, a);
            e->typ = make_type(a, TY_I32);
            return e->typ;
        }
        FuncSig *sig = lookup_func(e->call.name);
        if (!sig) { fprintf(stderr, "Undeclared function '%s'\n", e->call.name); exit(1); }
        if (e->call.argc != sig->param_count) {
            fprintf(stderr, "Wrong arg count for '%s': expected %zu got %zu\n",
                e->call.name, sig->param_count, e->call.argc);
            exit(1);
        }
        for (size_t i = 0; i < e->call.argc; i++) {
            Type *at = infer(e->call.args[i], ss, a);
            Type *pt = sig->param_types[i];
            // int literal argument adopts the parameter's int type
            if (is_int_kind(pt->kind) && relit(e->call.args[i], pt->kind, a))
                at = e->call.args[i]->typ;
            // implicit widening: narrower int arg widens to the param's int type
            if (is_int_kind(pt->kind)) { widen(&e->call.args[i], pt, a); at = e->call.args[i]->typ; }
            // Allow array-to-pointer decay: [N]T -> ^T
            bool compat = types_equal(at, pt);
            if (!compat && at->kind == TY_ARRAY && pt->kind == TY_PTR) {
                compat = types_equal(at->inner, pt->inner);
            }
            // Allow struct-value-to-pointer decay: Struct -> *Struct (caller
            // passes the struct's address, like arrays). Keeps the functional
            // stdlib API call-site-clean (vec_push(v,...) not vec_push(&v,...)).
            if (!compat && at->kind == TY_STRUCT && pt->kind == TY_PTR &&
                pt->inner && pt->inner->kind == TY_STRUCT &&
                at->struct_name && pt->inner->struct_name &&
                strcmp(at->struct_name, pt->inner->struct_name)==0) {
                compat = true;
            }
            if (!compat) {
                fprintf(stderr, "Arg %zu type mismatch in call to '%s'\n", i, e->call.name);
                exit(1);
            }
        }
        e->typ = clone_type(a, sig->ret_type);
        return e->typ;
    }
    case EX_ARRAYLIT: {
        if (e->arrlit.elemc == 0) { fprintf(stderr, "Empty array literal\n"); exit(1); }
        Type *et = infer(e->arrlit.elems[0], ss, a);
        for (size_t i = 1; i < e->arrlit.elemc; i++) {
            Type *it = infer(e->arrlit.elems[i], ss, a);
            if (!types_equal(it, et)) { fprintf(stderr, "Array literal type mismatch\n"); exit(1); }
        }
        Type *at = make_type(a, TY_ARRAY);
        at->inner = clone_type(a, et);
        at->array_size = (uint64_t)e->arrlit.elemc;
        e->typ = at;
        return e->typ;
    }
    case EX_ARRAYIDX: {
        Type *arr_t = lookup_var(ss, e->arridx.name);
        if (!arr_t) { fprintf(stderr, "Undeclared array '%s'\n", e->arridx.name); exit(1); }
        Type *elem_t = NULL;
        if (arr_t->kind == TY_ARRAY || arr_t->kind == TY_PTR) {
            elem_t = arr_t->inner;
        } else {
            fprintf(stderr, "Indexing non-array '%s'\n", e->arridx.name); exit(1);
        }
        infer(e->arridx.idx, ss, a);
        e->typ = clone_type(a, elem_t);
        return e->typ;
    }
    case EX_STRLIT: {
        /* String literals have type ^i8 */
        Type *inner = make_type(a, TY_I8);
        Type *t = make_type(a, TY_PTR);
        t->inner = inner;
        e->typ = t;
        return t;
    }
    case EX_CAST: {
        infer(e->cast.operand, ss, a);  /* type-check operand */
        /* Cast target type is exactly what the user specified */
        e->typ = e->cast.to;
        return e->typ;
    }
    }
    fprintf(stderr, "Unknown expr kind %d\n", e->kind); exit(1);
}

// ---- Check statements ----
static void check_stmt(Stmt *s, ScopeStack *ss, Type *ret_type, Arena *a);

static void check_stmts(Stmt **stmts, size_t n, ScopeStack *ss, Type *ret_type, Arena *a) {
    for (size_t i = 0; i < n; i++) check_stmt(stmts[i], ss, ret_type, a);
}

static void check_stmt(Stmt *s, ScopeStack *ss, Type *ret_type, Arena *a) {
    switch(s->kind) {
    case ST_VARDECL: {
        if (s->vardecl.init == NULL) {
            /* uninitialized declaration: type must be explicit */
            if (s->vardecl.typ == NULL) { fprintf(stderr,"var '%s' needs a type or initializer\n", s->vardecl.name); exit(1); }
            declare_var(ss, s->vardecl.name, s->vardecl.typ);
            break;
        }
        Type *it = infer(s->vardecl.init, ss, a);
        if (s->vardecl.typ == NULL) {
            /* := declaration: infer type from initializer */
            s->vardecl.typ = it;
        } else {
            Type *dt = s->vardecl.typ;
            /* scalar int literal adopts the declared int type */
            if (is_int_kind(dt->kind) && relit(s->vardecl.init, dt->kind, a))
                it = s->vardecl.init->typ;
            /* float literal adopts the declared float type (f32 vs f64) */
            if ((dt->kind==TY_F32||dt->kind==TY_F64) && relit_float(s->vardecl.init, dt->kind, a))
                it = s->vardecl.init->typ;
            /* implicit widening of a narrower int initialiser */
            if (is_int_kind(dt->kind)) { widen(&s->vardecl.init, dt, a); it = s->vardecl.init->typ; }
            /* array literal: coerce each int-literal element to declared elem type */
            if (dt->kind == TY_ARRAY && s->vardecl.init->kind == EX_ARRAYLIT &&
                is_int_kind(dt->inner->kind)) {
                Expr *al = s->vardecl.init;
                for (size_t i = 0; i < al->arrlit.elemc; i++)
                    relit(al->arrlit.elems[i], dt->inner->kind, a);
                it = dt; /* element-wise compatible now */
            }
            /* array fill shorthand: `name:[N]T = scalar` fills all N elements.
             * The scalar must match (coerce to) the element type. */
            else if (dt->kind == TY_ARRAY && s->vardecl.init->kind != EX_ARRAYLIT) {
                if (is_int_kind(dt->inner->kind)) {
                    relit(s->vardecl.init, dt->inner->kind, a);
                    widen(&s->vardecl.init, dt->inner, a);
                }
                if (!types_equal(s->vardecl.init->typ, dt->inner)) {
                    fprintf(stderr, "Array fill value type mismatch for '%s'\n", s->vardecl.name);
                    exit(1);
                }
                it = dt; /* fill is compatible */
            }
            if (!types_equal(it, dt)) {
                fprintf(stderr, "Type mismatch in var decl '%s'\n", s->vardecl.name);
                exit(1);
            }
        }
        declare_var(ss, s->vardecl.name, s->vardecl.typ);
        break;
    }
    case ST_ASSIGN: {
        Type *vt = lookup_var(ss, s->assign.name);
        if (!vt) { fprintf(stderr, "Undeclared var '%s'\n", s->assign.name); exit(1); }
        Type *rt = infer(s->assign.rhs, ss, a);
        if (is_int_kind(vt->kind)) { widen(&s->assign.rhs, vt, a); rt = s->assign.rhs->typ; }
        if (!types_equal(rt, vt)) {
            fprintf(stderr, "Type mismatch in assign '%s'\n", s->assign.name);
            exit(1);
        }
        break;
    }
    case ST_DEREFASSIGN: {
        Type *vt = lookup_var(ss, s->derefassign.name);
        if (!vt || vt->kind != TY_PTR) {
            fprintf(stderr, "Deref assign on non-pointer '%s'\n", s->derefassign.name);
            exit(1);
        }
        Type *rt = infer(s->derefassign.rhs, ss, a);
        if (is_int_kind(vt->inner->kind)) { widen(&s->derefassign.rhs, vt->inner, a); rt = s->derefassign.rhs->typ; }
        if (!types_equal(rt, vt->inner)) {
            fprintf(stderr, "Type mismatch in deref assign '%s'\n", s->derefassign.name);
            exit(1);
        }
        break;
    }
    case ST_IDXASSIGN: {
        Type *vt = lookup_var(ss, s->idxassign.name);
        if (!vt) { fprintf(stderr, "Undeclared var '%s' in indexed assign\n", s->idxassign.name); exit(1); }
        Type *elem_t = NULL;
        if (vt->kind == TY_ARRAY) elem_t = vt->inner;
        else if (vt->kind == TY_PTR) elem_t = vt->inner;
        else { fprintf(stderr, "Indexed assign on non-array/pointer '%s'\n", s->idxassign.name); exit(1); }
        infer(s->idxassign.idx, ss, a);
        Type *rt = infer(s->idxassign.rhs, ss, a);
        /* polymorphic int literal adopts the element type */
        if (is_int_kind(elem_t->kind) && relit(s->idxassign.rhs, elem_t->kind, a))
            rt = s->idxassign.rhs->typ;
        if (is_int_kind(elem_t->kind)) { widen(&s->idxassign.rhs, elem_t, a); rt = s->idxassign.rhs->typ; }
        if (!types_equal(rt, elem_t)) {
            fprintf(stderr, "Type mismatch in indexed assign '%s'\n", s->idxassign.name);
            exit(1);
        }
        break;
    }
    case ST_FIELDASSIGN: {
        Type *vt = lookup_var(ss, s->fieldassign.var);
        if (!vt) { fprintf(stderr, "Undeclared var '%s'\n", s->fieldassign.var); exit(1); }
        const char *sn = NULL;
        if (vt->kind == TY_STRUCT) sn = vt->struct_name;
        else if (vt->kind == TY_PTR && vt->inner && vt->inner->kind == TY_STRUCT) sn = vt->inner->struct_name;
        else { fprintf(stderr, "'%s' is not a struct\n", s->fieldassign.var); exit(1); }
        Type *ft = struct_field_type(sn, s->fieldassign.field);
        Type *rt = infer(s->fieldassign.rhs, ss, a);
        if (is_int_kind(ft->kind) && relit(s->fieldassign.rhs, ft->kind, a)) rt = s->fieldassign.rhs->typ;
        if (is_int_kind(ft->kind)) { widen(&s->fieldassign.rhs, ft, a); rt = s->fieldassign.rhs->typ; }
        /* array rvalue decays to a pointer to its element type */
        if (ft->kind == TY_PTR && rt->kind == TY_ARRAY && types_equal(ft->inner, rt->inner))
            rt = ft;
        if (!types_equal(rt, ft)) {
            fprintf(stderr, "Type mismatch in field assign '%s.%s'\n", s->fieldassign.var, s->fieldassign.field);
            exit(1);
        }
        break;
    }
    case ST_RETURN: {
        if (s->ret.val) {
            Type *vt = infer(s->ret.val, ss, a);
            /* int literal adopts the function's int return type */
            if (is_int_kind(ret_type->kind) && relit(s->ret.val, ret_type->kind, a))
                vt = s->ret.val->typ;
            if (is_int_kind(ret_type->kind)) { widen(&s->ret.val, ret_type, a); vt = s->ret.val->typ; }
            if (!types_equal(vt, ret_type)) {
                fprintf(stderr, "Return type mismatch\n");
                exit(1);
            }
        } else {
            if (ret_type->kind != TY_VOID) {
                fprintf(stderr, "Missing return value\n");
                exit(1);
            }
        }
        break;
    }
    case ST_IF: {
        infer(s->ifst.cond, ss, a);
        push_scope(ss);
        check_stmts(s->ifst.then_body, s->ifst.then_len, ss, ret_type, a);
        pop_scope(ss);
        if (s->ifst.else_body) {
            push_scope(ss);
            check_stmts(s->ifst.else_body, s->ifst.else_len, ss, ret_type, a);
            pop_scope(ss);
        }
        break;
    }
    case ST_FOR: {
        push_scope(ss);
        if (s->forst.has_init_decl) {
            Type *it = infer(s->forst.init_init, ss, a);
            if (!types_equal(it, s->forst.init_typ)) {
                fprintf(stderr, "Type mismatch in for init decl\n"); exit(1);
            }
            declare_var(ss, s->forst.init_name, s->forst.init_typ);
        } else if (s->forst.has_init_assign) {
            Type *vt = lookup_var(ss, s->forst.init_name);
            if (!vt) { fprintf(stderr, "Undeclared var '%s'\n", s->forst.init_name); exit(1); }
            Type *rt = infer(s->forst.init_rhs, ss, a);
            if (!types_equal(rt, vt)) { fprintf(stderr, "Type mismatch in for init assign\n"); exit(1); }
        }
        infer(s->forst.cond, ss, a);
        if (s->forst.has_step_assign) {
            Type *vt = lookup_var(ss, s->forst.step_name);
            if (!vt) { fprintf(stderr, "Undeclared var '%s'\n", s->forst.step_name); exit(1); }
            Type *rt = infer(s->forst.step_rhs, ss, a);
            if (!types_equal(rt, vt)) { fprintf(stderr, "Type mismatch in for step assign\n"); exit(1); }
        } else if (s->forst.step_expr) {
            infer(s->forst.step_expr, ss, a);
        }
        check_stmts(s->forst.body, s->forst.body_len, ss, ret_type, a);
        pop_scope(ss);
        break;
    }
    case ST_WHILE: {
        infer(s->whilest.cond, ss, a);
        push_scope(ss);
        check_stmts(s->whilest.body, s->whilest.body_len, ss, ret_type, a);
        pop_scope(ss);
        break;
    }
    case ST_EXPRSTMT:
        if (s->exprstmt) infer(s->exprstmt, ss, a);  /* NULL = empty-block/blank-line no-op */
        break;
    }
}

void typecheck(Program *prog, Arena *a) {
    // Register struct definitions
    g_structs = prog->structs;
    g_struct_count = prog->struct_count;
    // Collect function signatures
    func_count = 0;
    for (size_t i = 0; i < prog->func_count; i++) {
        FuncDef *f = &prog->funcs[i];
        if (func_count >= MAX_FUNCS) { fprintf(stderr, "Too many functions\n"); exit(1); }
        FuncSig *sig = &func_table[func_count++];
        sig->name = f->name;
        sig->param_types = f->param_types;
        sig->param_count = f->param_count;
        sig->ret_type = f->ret_type;
    }

    // Type-check bodies
    ScopeStack ss = {0};
    for (size_t i = 0; i < prog->func_count; i++) {
        FuncDef *f = &prog->funcs[i];
        push_scope(&ss);
        for (size_t j = 0; j < f->param_count; j++) {
            declare_var(&ss, f->param_names[j], f->param_types[j]);
        }
        check_stmts(f->body, f->body_len, &ss, f->ret_type, a);
        pop_scope(&ss);
    }
}
