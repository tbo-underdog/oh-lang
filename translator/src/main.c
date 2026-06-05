/*
 * Oh -> Human-Readable Translator
 *
 * Reads a .oh file, parses it using the Oh lexer/parser,
 * then pretty-prints the AST as readable pseudo-C to stdout.
 */

#include "../../compiler/src/lexer.h"
#include "../../compiler/src/parser.h"

/* ------------------------------------------------------------------ */
/* Output helpers                                                       */
/* ------------------------------------------------------------------ */

static int indent_level = 0;

static void print_indent(void) {
    for (int i = 0; i < indent_level; i++)
        printf("    ");
}

/* ------------------------------------------------------------------ */
/* Type printing                                                        */
/* ------------------------------------------------------------------ */

static void print_type(Type *t) {
    if (!t) { printf("void"); return; }
    switch (t->kind) {
    case TY_I8:    printf("i8");    return;
    case TY_I16:   printf("i16");   return;
    case TY_I32:   printf("i32");   return;
    case TY_I64:   printf("i64");   return;
    case TY_U8:    printf("u8");    return;
    case TY_U16:   printf("u16");   return;
    case TY_U32:   printf("u32");   return;
    case TY_U64:   printf("u64");   return;
    case TY_F32:   printf("f32");   return;
    case TY_F64:   printf("f64");   return;
    case TY_BOOL:  printf("bool");  return;
    case TY_VOID:  printf("void");  return;
    case TY_PTR:
        printf("*");
        print_type(t->inner);
        return;
    case TY_ARRAY:
        printf("[%llu]", (unsigned long long)t->array_size);
        print_type(t->inner);
        return;
    case TY_STRUCT:
        printf("%s", t->struct_name);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Expression printing                                                  */
/* ------------------------------------------------------------------ */

/* Returns true if this expression needs parentheses when used as a
 * sub-expression. We use a simple precedence heuristic. */

static int expr_prec(Expr *e) {
    if (!e) return 100;
    if (e->kind == EX_BINOP) {
        switch (e->binop.op) {        /* higher binds tighter (C-like) */
        case OP_OR:     return 1;
        case OP_AND:    return 2;
        case OP_BITOR:  return 3;
        case OP_XOR:    return 4;
        case OP_BITAND: return 5;
        case OP_EQ:
        case OP_NEQ:    return 6;
        case OP_LT:
        case OP_GT:
        case OP_LTEQ:
        case OP_GTEQ:   return 7;
        case OP_LSHIFT:
        case OP_RSHIFT: return 8;
        case OP_ADD:
        case OP_SUB:    return 9;
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:    return 10;
        }
    }
    if (e->kind == EX_UNOP) return 11;
    return 12; /* atoms */
}
/* parent precedence used when printing the operand of a unary op or a cast:
 * above every binary operator, so any binop operand is parenthesized. */
#define UNARY_OPND_PREC 11

static const char *binop_str(BinOp op) {
    switch (op) {
    case OP_ADD:  return "+";
    case OP_SUB:  return "-";
    case OP_MUL:  return "*";
    case OP_DIV:  return "/";
    case OP_MOD:  return "%";
    case OP_EQ:   return "==";
    case OP_NEQ:  return "!=";
    case OP_LT:   return "<";
    case OP_GT:   return ">";
    case OP_LTEQ: return "<=";
    case OP_GTEQ: return ">=";
    case OP_AND:  return "&&";
    case OP_OR:   return "||";
    case OP_BITOR:  return "|";
    case OP_BITAND: return "&";
    case OP_XOR:    return "^";
    case OP_LSHIFT: return "<<";
    case OP_RSHIFT: return ">>";
    }
    return "?";
}

static void print_expr(Expr *e);

static void print_expr_paren(Expr *child, int parent_prec) {
    int cp = expr_prec(child);
    if (cp < parent_prec) {
        printf("(");
        print_expr(child);
        printf(")");
    } else {
        print_expr(child);
    }
}

static void print_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EX_INTLIT:
        printf("%lld", (long long)e->ival);
        return;
    case EX_FLOATLIT:
        printf("%g", e->fval);
        return;
    case EX_BOOLLIT:
        printf("%s", e->bval ? "true" : "false");
        return;
    case EX_IDENT:
        printf("%s", e->ident);
        return;
    case EX_BINOP: {
        int my_prec = expr_prec(e);
        print_expr_paren(e->binop.lhs, my_prec);
        printf(" %s ", binop_str(e->binop.op));
        /* Right side: use my_prec+1 so same-precedence chains stay readable */
        print_expr_paren(e->binop.rhs, my_prec + 1);
        return;
    }
    case EX_UNOP:
        switch (e->unop.op) {
        case UOP_NEG:
            printf("-");
            print_expr_paren(e->unop.operand, UNARY_OPND_PREC);
            return;
        case UOP_NOT:
            printf("!");
            print_expr_paren(e->unop.operand, UNARY_OPND_PREC);
            return;
        case UOP_ADDROF:
            printf("&");
            print_expr(e->unop.operand);
            return;
        case UOP_DEREF:
            printf("*");
            print_expr_paren(e->unop.operand, UNARY_OPND_PREC);
            return;
        case UOP_BITNOT:
            printf("~");
            print_expr_paren(e->unop.operand, UNARY_OPND_PREC);
            return;
        }
        return;
    case EX_TERNARY:
        print_expr_paren(e->ternary.cond, 1);
        printf(" ? ");
        print_expr(e->ternary.then_e);
        printf(" : ");
        print_expr(e->ternary.else_e);
        return;
    case EX_CALL:
        printf("%s(", e->call.name);
        for (size_t i = 0; i < e->call.argc; i++) {
            if (i > 0) printf(", ");
            print_expr(e->call.args[i]);
        }
        printf(")");
        return;
    case EX_ARRAYLIT:
        printf("{");
        for (size_t i = 0; i < e->arrlit.elemc; i++) {
            if (i > 0) printf(", ");
            print_expr(e->arrlit.elems[i]);
        }
        printf("}");
        return;
    case EX_ARRAYIDX:
        printf("%s[", e->arridx.name);
        print_expr(e->arridx.idx);
        printf("]");
        return;
    case EX_STRLIT:
        printf("\"%s\"", e->sval);
        return;
    case EX_CAST:
        printf("(");
        print_type(e->cast.to);
        printf(")");
        print_expr_paren(e->cast.operand, UNARY_OPND_PREC);
        return;
    case EX_FIELD:
        printf("%s.%s", e->field.var, e->field.field);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Statement printing                                                   */
/* ------------------------------------------------------------------ */

static void print_stmt(Stmt *s);

static void print_stmts(Stmt **body, size_t n) {
    for (size_t i = 0; i < n; i++)
        print_stmt(body[i]);
}

static void print_stmt(Stmt *s) {
    switch (s->kind) {

    case ST_VARDECL:
        print_indent();
        if (s->vardecl.typ == NULL) {
            /* type-inferred (:=) declaration */
            printf("%s := ", s->vardecl.name);
            print_expr(s->vardecl.init);
        } else if (s->vardecl.init == NULL) {
            /* uninitialized declaration (e.g. scratch buffer) */
            printf("%s: ", s->vardecl.name);
            print_type(s->vardecl.typ);
        } else {
            printf("%s: ", s->vardecl.name);
            print_type(s->vardecl.typ);
            printf(" = ");
            print_expr(s->vardecl.init);
        }
        printf(";\n");
        return;

    case ST_ASSIGN:
        print_indent();
        printf("%s = ", s->assign.name);
        print_expr(s->assign.rhs);
        printf(";\n");
        return;

    case ST_DEREFASSIGN:
        print_indent();
        printf("*%s = ", s->derefassign.name);
        print_expr(s->derefassign.rhs);
        printf(";\n");
        return;

    case ST_RETURN:
        print_indent();
        printf("return");
        if (s->ret.val) {
            printf(" ");
            print_expr(s->ret.val);
        }
        printf(";\n");
        return;

    case ST_IF:
        print_indent();
        printf("if (");
        print_expr(s->ifst.cond);
        printf(") {\n");
        indent_level++;
        print_stmts(s->ifst.then_body, s->ifst.then_len);
        indent_level--;
        if (s->ifst.else_body && s->ifst.else_len > 0) {
            print_indent();
            printf("} else {\n");
            indent_level++;
            print_stmts(s->ifst.else_body, s->ifst.else_len);
            indent_level--;
        }
        print_indent();
        printf("}\n");
        return;

    case ST_FOR: {
        print_indent();
        printf("for (");
        /* init */
        if (s->forst.has_init_decl) {
            printf("%s: ", s->forst.init_name);
            print_type(s->forst.init_typ);
            printf(" = ");
            print_expr(s->forst.init_init);
        } else if (s->forst.has_init_assign) {
            printf("%s = ", s->forst.init_name);
            print_expr(s->forst.init_rhs);
        }
        printf("; ");
        /* cond */
        print_expr(s->forst.cond);
        printf("; ");
        /* step */
        if (s->forst.has_step_assign) {
            printf("%s = ", s->forst.step_name);
            print_expr(s->forst.step_rhs);
        } else if (s->forst.step_expr) {
            print_expr(s->forst.step_expr);
        }
        printf(") {\n");
        indent_level++;
        print_stmts(s->forst.body, s->forst.body_len);
        indent_level--;
        print_indent();
        printf("}\n");
        return;
    }

    case ST_WHILE:
        print_indent();
        printf("while (");
        print_expr(s->whilest.cond);
        printf(") {\n");
        indent_level++;
        print_stmts(s->whilest.body, s->whilest.body_len);
        indent_level--;
        print_indent();
        printf("}\n");
        return;

    case ST_IDXASSIGN:
        print_indent();
        printf("%s[", s->idxassign.name);
        print_expr(s->idxassign.idx);
        printf("] = ");
        print_expr(s->idxassign.rhs);
        printf(";\n");
        return;

    case ST_FIELDASSIGN:
        print_indent();
        printf("%s.%s = ", s->fieldassign.var, s->fieldassign.field);
        print_expr(s->fieldassign.rhs);
        printf(";\n");
        return;

    case ST_EXPRSTMT:
        print_indent();
        print_expr(s->exprstmt);
        printf(";\n");
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Function printing                                                    */
/* ------------------------------------------------------------------ */

static void print_struct(StructDef *sd) {
    printf("struct %s {\n", sd->name);
    for (size_t i = 0; i < sd->field_count; i++) {
        printf("    %s: ", sd->field_names[i]);
        print_type(sd->field_types[i]);
        printf(";\n");
    }
    printf("}\n");
}

static void print_func(FuncDef *f) {
    printf("function %s(", f->name);
    for (size_t i = 0; i < f->param_count; i++) {
        if (i > 0) printf(", ");
        printf("%s: ", f->param_names[i]);
        print_type(f->param_types[i]);
    }
    printf(") -> ");
    print_type(f->ret_type);
    printf(" {\n");
    indent_level = 1;
    print_stmts(f->body, f->body_len);
    indent_level = 0;
    printf("}\n");
}

/* ------------------------------------------------------------------ */
/* File reader                                                           */
/* ------------------------------------------------------------------ */

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open '%s'\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)(sz + 1));
    if (!buf) { fprintf(stderr, "OOM\n"); exit(1); }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    (void)n;
    buf[sz] = '\0';
    fclose(fp);
    return buf;
}

/* ------------------------------------------------------------------ */
/* main                                                                  */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ohtranslate <file.oh>\n");
        return 1;
    }

    const char *path = argv[1];
    char *src = read_file(path);

    /* 16 MB arena — same as the compiler */
    Arena a = arena_new(16 * 1024 * 1024);

    /* Lex */
    TokenArray tokens = lex(src, &a);

    /* Parse — no type-checking needed for pretty-printing */
    Program prog = parse(tokens, &a);

    /* Pretty-print struct definitions first, then each function */
    for (size_t i = 0; i < prog.struct_count; i++) {
        if (i > 0) printf("\n");
        print_struct(&prog.structs[i]);
    }
    for (size_t i = 0; i < prog.func_count; i++) {
        if (i > 0 || prog.struct_count > 0) printf("\n");
        print_func(&prog.funcs[i]);
    }

    free(src);
    arena_free(&a);
    return 0;
}
