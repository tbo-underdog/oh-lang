#pragma once
#include "lexer.h"

// ---- Types ----
typedef enum {
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64,
    TY_F32, TY_F64,
    TY_BOOL,
    TY_VOID,
    TY_PTR,
    TY_ARRAY,
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    Type    *inner;       // for PTR and ARRAY
    uint64_t array_size;  // for ARRAY
};

// ---- Expressions ----
typedef enum {
    EX_INTLIT, EX_FLOATLIT, EX_BOOLLIT,
    EX_STRLIT,
    EX_IDENT,
    EX_BINOP,
    EX_UNOP,
    EX_CALL,
    EX_ARRAYLIT,
    EX_ARRAYIDX,
    EX_CAST,
    EX_TERNARY,     /* cond ? then : else — value expression */
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTEQ, OP_GTEQ,
    OP_AND, OP_OR,
    OP_BITOR,   /* |  bitwise OR  */
    OP_BITAND,  /* &  bitwise AND (binary) */
    OP_XOR,     /* ^  bitwise XOR */
    OP_LSHIFT,  /* << left shift  */
    OP_RSHIFT,  /* >> right shift */
} BinOp;

typedef enum {
    UOP_NEG, UOP_NOT, UOP_ADDROF, UOP_DEREF, UOP_BITNOT,
} UnOp;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    Type    *typ;  // filled by typechecker
    union {
        int64_t  ival;
        double   fval;
        bool     bval;
        char    *ident;
        char    *sval;  /* for EX_STRLIT: raw string content */
        struct { BinOp op; Expr *lhs; Expr *rhs; } binop;
        struct { UnOp op; Expr *operand; } unop;
        struct { char *name; Expr **args; size_t argc; } call;
        struct { Expr **elems; size_t elemc; } arrlit;
        struct { char *name; Expr *idx; } arridx;
        struct { Type *to; Expr *operand; } cast;
        struct { Expr *cond; Expr *then_e; Expr *else_e; } ternary; /* EX_TERNARY */
    };
};

// ---- Statements ----
typedef enum {
    ST_VARDECL,
    ST_ASSIGN,
    ST_DEREFASSIGN,
    ST_IDXASSIGN,   /* name%idx = rhs — store through pointer or array at index */
    ST_RETURN,
    ST_IF,
    ST_FOR,
    ST_WHILE,
    ST_EXPRSTMT,
} StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    union {
        struct { char *name; Type *typ; Expr *init; int cg_reg; } vardecl; /* typ==NULL → infer; cg_reg set by alloca-hoist pass */
        struct { char *name; Expr *rhs; } assign;
        struct { char *name; Expr *rhs; } derefassign;
        struct { char *name; Expr *idx; Expr *rhs; } idxassign; /* ST_IDXASSIGN */
        struct { Expr *val; } ret;   // val may be NULL for void return
        struct {
            Expr  *cond;
            Stmt **then_body; size_t then_len;
            Stmt **else_body; size_t else_len; // else_body may be NULL
        } ifst;
        struct {
            // init: either init_decl or init_assign (or neither)
            bool has_init_decl;
            char *init_name; Type *init_typ; Expr *init_init; int cg_reg; // for decl; cg_reg set by hoist pass
            bool has_init_assign;
            // init_name reused for assign too
            Expr *init_rhs;   // for assign
            // cond & step
            Expr *cond;
            bool has_step_assign;
            char *step_name; Expr *step_rhs;
            Expr *step_expr;  // if not assign
            Stmt **body; size_t body_len;
        } forst;
        struct { Expr *cond; Stmt **body; size_t body_len; } whilest;
        Expr *exprstmt;
    };
};

// ---- Function & Program ----
typedef struct {
    char  *name;
    char **param_names;
    Type **param_types;
    size_t param_count;
    Type  *ret_type;
    Stmt **body;
    size_t body_len;
} FuncDef;

typedef struct {
    FuncDef *funcs;
    size_t   func_count;
} Program;

Program parse(TokenArray tokens, Arena *a);
