#include "parser.h"

typedef struct { Token *tokens; size_t len; size_t pos; Arena *a;
                 char **struct_names; size_t struct_name_count; } Parser;

static bool is_struct_name(Parser *p, const char *s) {
    for (size_t i=0;i<p->struct_name_count;i++)
        if (strcmp(p->struct_names[i],s)==0) return true;
    return false;
}

static Token  peek(Parser *p)         { return p->tokens[p->pos]; }
static TokenKind peekk(Parser *p)     { return p->tokens[p->pos].kind; }
static Token  advance(Parser *p)      { Token t=p->tokens[p->pos]; if(p->pos+1<p->len) p->pos++; return t; }
static bool   check(Parser *p, TokenKind k) { return peekk(p)==k; }
static bool   match(Parser *p, TokenKind k) { if(check(p,k)){advance(p);return true;} return false; }
static Token  expect(Parser *p, TokenKind k) {
    Token t=peek(p);
    if(t.kind!=k){ fprintf(stderr,"Parse error: expected %s got %s '%s' at %u:%u\n",
        token_kind_name(k),token_kind_name(t.kind),t.text,t.line,t.col); exit(1); }
    return advance(p);
}

/* ---- Type parsing ----
 * v3 types: 3=i32 6=i64 1=i8 2=i16 f=f32 d=f64 b=bool v=void *T=ptr [N]T=array
 * Type tokens: TK_INTLIT(1/2/3/6), TK_IDENT(f/d/b/v), TK_STAR, TK_LBRACKET
 */
static Type *parse_type(Parser *p) {
    Type *t = (Type*)arena_alloc(p->a, sizeof(Type));
    memset(t, 0, sizeof(*t));
    if (check(p, TK_STAR)) {
        advance(p); t->kind = TY_PTR; t->inner = parse_type(p); return t;
    }
    if (check(p, TK_LBRACKET)) {
        advance(p);
        Token sz = expect(p, TK_INTLIT);
        t->array_size = (uint64_t)strtoul(sz.text, NULL, 10);
        expect(p, TK_RBRACKET);
        t->kind = TY_ARRAY; t->inner = parse_type(p); return t;
    }
    /* Integer literal type codes */
    if (check(p, TK_INTLIT)) {
        Token tn = advance(p);
        long v = strtol(tn.text, NULL, 10);
        if      (v==1) t->kind=TY_I8;
        else if (v==2) t->kind=TY_I16;
        else if (v==3) t->kind=TY_I32;
        else if (v==6) t->kind=TY_I64;
        else { fprintf(stderr,"Unknown int type code %ld at %u:%u\n",v,tn.line,tn.col); exit(1); }
        return t;
    }
    /* Identifier type codes: f=f32 d=f64 b=bool v=void */
    if (check(p, TK_IDENT)) {
        /* struct name? */
        if (is_struct_name(p, peek(p).text)) {
            Token tn = advance(p);
            t->kind = TY_STRUCT; t->struct_name = tn.text; return t;
        }
        Token tn = advance(p);
        if      (strcmp(tn.text,"f")==0) t->kind=TY_F32;
        else if (strcmp(tn.text,"d")==0) t->kind=TY_F64;
        else if (strcmp(tn.text,"b")==0) t->kind=TY_BOOL;
        else if (strcmp(tn.text,"v")==0) t->kind=TY_VOID;
        /* `c` = char/byte string pointer = *i8 (very common as a string param) */
        else if (strcmp(tn.text,"c")==0) { t->kind=TY_PTR; Type*in=(Type*)arena_alloc(p->a,sizeof(Type)); memset(in,0,sizeof(*in)); in->kind=TY_I8; t->inner=in; }
        /* legacy type names still accepted */
        else if (strcmp(tn.text,"i8") ==0) t->kind=TY_I8;
        else if (strcmp(tn.text,"i16")==0) t->kind=TY_I16;
        else if (strcmp(tn.text,"i32")==0) t->kind=TY_I32;
        else if (strcmp(tn.text,"i64")==0) t->kind=TY_I64;
        else if (strcmp(tn.text,"u8") ==0) t->kind=TY_U8;
        else if (strcmp(tn.text,"u16")==0) t->kind=TY_U16;
        else if (strcmp(tn.text,"u32")==0) t->kind=TY_U32;
        else if (strcmp(tn.text,"u64")==0) t->kind=TY_U64;
        else if (strcmp(tn.text,"f32")==0) t->kind=TY_F32;
        else if (strcmp(tn.text,"f64")==0) t->kind=TY_F64;
        else { fprintf(stderr,"Unknown type '%s' at %u:%u\n",tn.text,tn.line,tn.col); exit(1); }
        return t;
    }
    Token bad=peek(p);
    fprintf(stderr,"Expected type at %u:%u got %s '%s'\n",bad.line,bad.col,token_kind_name(bad.kind),bad.text);
    exit(1);
}

/* ---- Expression parsing ---- */
static Expr *parse_expr(Parser *p);
static Expr *parse_unary(Parser *p);
static Expr *parse_primary(Parser *p);
static Expr *new_expr(Parser *p, ExprKind k) {
    Expr *e=(Expr*)arena_alloc(p->a,sizeof(Expr)); e->kind=k; e->typ=NULL; return e;
}

static Expr *parse_mul(Parser *p) {
    Expr *lhs=parse_unary(p);
    while(check(p,TK_STAR)||check(p,TK_SLASH)||check(p,TK_PERCENT)) {
        TokenKind tk=peekk(p); advance(p);
        BinOp op=(tk==TK_STAR)?OP_MUL:(tk==TK_SLASH)?OP_DIV:OP_MOD;
        Expr *rhs=parse_unary(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=op; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_add(Parser *p) {
    Expr *lhs=parse_mul(p);
    while(check(p,TK_PLUS)||check(p,TK_MINUS)) {
        BinOp op=(peekk(p)==TK_PLUS)?OP_ADD:OP_SUB; advance(p);
        Expr *rhs=parse_mul(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=op; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_shift(Parser *p) {
    Expr *lhs=parse_add(p);
    while(check(p,TK_LSHIFT)||check(p,TK_GT2)) {
        BinOp op=(peekk(p)==TK_LSHIFT)?OP_LSHIFT:OP_RSHIFT; advance(p);
        Expr *rhs=parse_add(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=op; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_rel(Parser *p) {
    Expr *lhs=parse_shift(p);
    while(check(p,TK_LT)||check(p,TK_GT)||check(p,TK_LTEQ)||check(p,TK_GTEQ)) {
        TokenKind tk=peekk(p); advance(p);
        BinOp op=(tk==TK_LT)?OP_LT:(tk==TK_GT)?OP_GT:(tk==TK_LTEQ)?OP_LTEQ:OP_GTEQ;
        Expr *rhs=parse_shift(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=op; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_eq(Parser *p) {
    Expr *lhs=parse_rel(p);
    while(check(p,TK_EQEQ)||check(p,TK_BANGEQ)) {
        BinOp op=(peekk(p)==TK_EQEQ)?OP_EQ:OP_NEQ; advance(p);
        Expr *rhs=parse_rel(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=op; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_bitand(Parser *p) {
    Expr *lhs=parse_eq(p);
    while(check(p,TK_AMP)) {
        advance(p); Expr *rhs=parse_eq(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=OP_BITAND; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_xor(Parser *p) {
    Expr *lhs=parse_bitand(p);
    while(check(p,TK_CARET)) {
        advance(p); Expr *rhs=parse_bitand(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=OP_XOR; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_bitor(Parser *p) {
    Expr *lhs=parse_xor(p);
    while(check(p,TK_PIPE)) {
        advance(p); Expr *rhs=parse_xor(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=OP_BITOR; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_logand(Parser *p) {
    Expr *lhs=parse_bitor(p);
    while(check(p,TK_AMPAMP)) {
        advance(p); Expr *rhs=parse_bitor(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=OP_AND; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}
static Expr *parse_logor(Parser *p) {
    Expr *lhs=parse_logand(p);
    while(check(p,TK_PIPEPIPE)) {
        advance(p); Expr *rhs=parse_logand(p);
        Expr *e=new_expr(p,EX_BINOP); e->binop.op=OP_OR; e->binop.lhs=lhs; e->binop.rhs=rhs; lhs=e;
    }
    return lhs;
}

/* Ternary: logor ? logor : logor
 * cond?a:b — replaces simple if-return patterns */
static Expr *parse_ternary(Parser *p) {
    Expr *lhs = parse_logor(p);
    if (check(p, TK_QUESTION)) {
        advance(p);
        /* branches parse as full ternaries -> nesting + right-associativity:
         * a?b:c?d:e == a?b:(c?d:e); a?b?c:d:e == a?(b?c:d):e */
        Expr *then_e = parse_ternary(p);
        expect(p, TK_COLON);
        Expr *else_e = parse_ternary(p);
        Expr *e = new_expr(p, EX_TERNARY);
        e->ternary.cond   = lhs;
        e->ternary.then_e = then_e;
        e->ternary.else_e = else_e;
        return e;
    }
    return lhs;
}

static Expr *parse_expr(Parser *p) { return parse_ternary(p); }

static Expr *parse_unary(Parser *p) {
    if (check(p,TK_MINUS)) {
        advance(p); Expr *op=parse_unary(p);
        Expr *e=new_expr(p,EX_UNOP); e->unop.op=UOP_NEG; e->unop.operand=op; return e;
    }
    if (check(p,TK_BANG)) {
        advance(p); Expr *op=parse_unary(p);
        Expr *e=new_expr(p,EX_UNOP); e->unop.op=UOP_DEREF; e->unop.operand=op; return e;
    }
    if (check(p,TK_AMP)) {
        advance(p); Expr *op=parse_unary(p);
        Expr *e=new_expr(p,EX_UNOP); e->unop.op=UOP_ADDROF; e->unop.operand=op; return e;
    }
    if (check(p,TK_TILDE)) {
        advance(p); Expr *op=parse_unary(p);
        Expr *e=new_expr(p,EX_UNOP); e->unop.op=UOP_BITNOT; e->unop.operand=op; return e;
    }
    return parse_primary(p);
}

/* Helper: check if current token starts a type (for cast lookahead) */
static bool peek_is_type_start(Parser *p) {
    TokenKind k = peekk(p);
    if (k == TK_STAR || k == TK_LBRACKET) return true;
    if (k == TK_INTLIT) {
        long v = strtol(p->tokens[p->pos].text, NULL, 10);
        return v==1||v==2||v==3||v==6;
    }
    if (k == TK_IDENT) {
        const char *t = p->tokens[p->pos].text;
        return strcmp(t,"f")==0||strcmp(t,"d")==0||strcmp(t,"b")==0||strcmp(t,"v")==0||
               strcmp(t,"i8")==0||strcmp(t,"i16")==0||strcmp(t,"i32")==0||strcmp(t,"i64")==0||
               strcmp(t,"u8")==0||strcmp(t,"u16")==0||strcmp(t,"u32")==0||strcmp(t,"u64")==0||
               strcmp(t,"f32")==0||strcmp(t,"f64")==0;
    }
    return false;
}

static Expr *parse_primary(Parser *p) {
    Token t = peek(p);
    switch(t.kind) {
    case TK_INTLIT: {
        advance(p); Expr *e=new_expr(p,EX_INTLIT);
        e->ival=(int64_t)strtoll(t.text,NULL,10); return e;
    }
    case TK_FLOATLIT: {
        advance(p); Expr *e=new_expr(p,EX_FLOATLIT);
        e->fval=strtod(t.text,NULL); return e;
    }
    case TK_BOOLTRUE: {
        advance(p); Expr *e=new_expr(p,EX_BOOLLIT); e->bval=true; return e;
    }
    case TK_STRLIT: {
        advance(p); Expr *e=new_expr(p,EX_STRLIT); e->sval=t.text; return e;
    }
    case TK_LBRACKET: {
        /* Array literal [e1, e2, ...] */
        advance(p);
        DYNARRAY(Expr*) elems={0};
        if (!check(p,TK_RBRACKET)) {
            Expr *el=parse_expr(p); DA_PUSH(&elems,el,p->a);
            while(match(p,TK_COMMA)){el=parse_expr(p); DA_PUSH(&elems,el,p->a);}
        }
        expect(p,TK_RBRACKET);
        Expr *e=new_expr(p,EX_ARRAYLIT); e->arrlit.elems=elems.data; e->arrlit.elemc=elems.len; return e;
    }
    case TK_LPAREN: {
        advance(p);
        /* Cast lookahead: (type)expr */
        bool is_cast = false;
        size_t saved = p->pos;
        if (peek_is_type_start(p)) {
            /* Try to consume a type and see if next is ')' */
            size_t tmp = p->pos;
            /* Skip pointer stars */
            while (tmp<p->len && p->tokens[tmp].kind==TK_STAR) tmp++;
            /* Skip [N] */
            if (tmp<p->len && p->tokens[tmp].kind==TK_LBRACKET) {
                tmp++; while(tmp<p->len&&p->tokens[tmp].kind!=TK_RBRACKET) tmp++;
                if(tmp<p->len) tmp++;
            }
            /* Skip base type token */
            if (tmp<p->len && (p->tokens[tmp].kind==TK_INTLIT||p->tokens[tmp].kind==TK_IDENT)) tmp++;
            if (tmp<p->len && p->tokens[tmp].kind==TK_RPAREN) is_cast=true;
        }
        if (is_cast) {
            Type *ct=parse_type(p); expect(p,TK_RPAREN);
            Expr *op=parse_unary(p);
            Expr *e=new_expr(p,EX_CAST); e->cast.to=ct; e->cast.operand=op; return e;
        }
        (void)saved;
        Expr *inner=parse_expr(p); expect(p,TK_RPAREN); return inner;
    }
    case TK_IDENT: {
        advance(p);
        /* Function call: name ( args ) */
        if (check(p,TK_LPAREN)) {
            advance(p);
            DYNARRAY(Expr*) args={0};
            if (!check(p,TK_RPAREN)) {
                Expr *a=parse_expr(p); DA_PUSH(&args,a,p->a);
                while(match(p,TK_COMMA)){a=parse_expr(p); DA_PUSH(&args,a,p->a);}
            }
            expect(p,TK_RPAREN);
            Expr *e=new_expr(p,EX_CALL); e->call.name=t.text;
            e->call.args=args.data; e->call.argc=args.len; return e;
        }
        /* Array index: name [ expr ] */
        if (check(p,TK_LBRACKET)) {
            advance(p); Expr *idx=parse_expr(p); expect(p,TK_RBRACKET);
            Expr *e=new_expr(p,EX_ARRAYIDX); e->arridx.name=t.text; e->arridx.idx=idx; return e;
        }
        /* Struct field read: var . field */
        if (check(p,TK_DOT)) {
            advance(p); Token f=expect(p,TK_IDENT);
            Expr *e=new_expr(p,EX_FIELD); e->field.var=t.text; e->field.field=f.text; return e;
        }
        /* Plain identifier */
        Expr *e=new_expr(p,EX_IDENT); e->ident=t.text; return e;
    }
    default:
        fprintf(stderr,"Parse error: unexpected %s '%s' at %u:%u in expression\n",
            token_kind_name(t.kind),t.text,t.line,t.col);
        exit(1);
    }
}

/* ---- Statement parsing ---- */
/* Compound assignment: if the current token is an op immediately followed by
 * '=' (e.g. `+=`), consume both and return 1 with the BinOp. Enables
 * `x+=e` / `a[i]*=e` / `!p|=e` as sugar for `x=x+e` etc. */
static int compound_op(Parser *p, BinOp *op) {
    if (p->pos + 1 >= p->len || p->tokens[p->pos+1].kind != TK_EQ) return 0;
    BinOp o;
    switch (peekk(p)) {
    case TK_PLUS:  o=OP_ADD;    break;
    case TK_MINUS: o=OP_SUB;    break;
    case TK_STAR:  o=OP_MUL;    break;
    case TK_SLASH: o=OP_DIV;    break;
    case TK_AMP:   o=OP_BITAND; break;
    case TK_PIPE:  o=OP_BITOR;  break;
    case TK_CARET: o=OP_XOR;    break;
    default: return 0;
    }
    advance(p); advance(p); /* consume op and '=' */
    *op = o; return 1;
}
static Stmt *parse_stmt(Parser *p);
static Stmt **parse_stmt_list(Parser *p, TokenKind terminator, size_t *out_len) {
    DYNARRAY(Stmt*) stmts={0};
    while(!check(p,terminator)&&!check(p,TK_EOF)){
        Stmt *s=parse_stmt(p); DA_PUSH(&stmts,s,p->a);
    }
    *out_len=stmts.len; return stmts.data;
}

static Stmt *parse_stmt(Parser *p) {
    Stmt *s=(Stmt*)arena_alloc(p->a,sizeof(Stmt));
    memset(s,0,sizeof(*s));

    /* Skip optional semicolons between statements */
    while(check(p,TK_SEMI)) advance(p);

    switch(peekk(p)) {

    /* \ expr ;  — explicit return */
    case TK_BACKSLASH: {
        advance(p); s->kind=ST_RETURN;
        if(check(p,TK_SEMI)||check(p,TK_RBRACE)) { match(p,TK_SEMI); s->ret.val=NULL; }
        else { s->ret.val=parse_expr(p); match(p,TK_SEMI); }
        break;
    }

    /* ? expr { then } : { else }  — if statement */
    case TK_QUESTION: {
        advance(p); Expr *cond=parse_expr(p);
        expect(p,TK_LBRACE);
        size_t tlen=0; Stmt **tbody=parse_stmt_list(p,TK_RBRACE,&tlen); expect(p,TK_RBRACE);
        Stmt **ebody=NULL; size_t elen=0;
        if(match(p,TK_COLON)){expect(p,TK_LBRACE);ebody=parse_stmt_list(p,TK_RBRACE,&elen);expect(p,TK_RBRACE);}
        s->kind=ST_IF; s->ifst.cond=cond;
        s->ifst.then_body=tbody; s->ifst.then_len=tlen;
        s->ifst.else_body=ebody; s->ifst.else_len=elen;
        break;
    }

    /* L var , end { body }       — for 0..end  (implicit start=0, step=+1)
     * L var = start , end { body } — for start..end (explicit)
     * L var = start , end , step { body } — explicit step  */
    case TK_AT: {
        advance(p); s->kind=ST_FOR; memset(&s->forst,0,sizeof(s->forst));
        Token vname=expect(p,TK_IDENT);

        /* Default i32 for loop variable */
        Type *loop_type=(Type*)arena_alloc(p->a,sizeof(Type));
        memset(loop_type,0,sizeof(*loop_type)); loop_type->kind=TY_I32;

        if(check(p,TK_EQ)) {
            /* Explicit start: L var = start , end [ , step ] { } */
            advance(p);
            Expr *start=parse_expr(p); expect(p,TK_COMMA);
            Expr *end=parse_expr(p);
            Expr *step_expr=NULL;
            if(match(p,TK_COMMA)) step_expr=parse_expr(p);
            expect(p,TK_LBRACE);
            size_t blen=0; Stmt **body=parse_stmt_list(p,TK_RBRACE,&blen); expect(p,TK_RBRACE);

            s->forst.has_init_decl=true;
            s->forst.init_name=vname.text; s->forst.init_typ=loop_type; s->forst.init_init=start;

            /* cond: var < end */
            Expr *cond_e=new_expr(p,EX_BINOP); cond_e->binop.op=OP_LT;
            Expr *vid=new_expr(p,EX_IDENT); vid->ident=vname.text;
            cond_e->binop.lhs=vid; cond_e->binop.rhs=end;
            s->forst.cond=cond_e;

            /* step: var = var + step (or var = var + 1) */
            s->forst.has_step_assign=true; s->forst.step_name=vname.text;
            Expr *v2=new_expr(p,EX_IDENT); v2->ident=vname.text;
            if(!step_expr){step_expr=new_expr(p,EX_INTLIT); step_expr->ival=1;}
            Expr *add=new_expr(p,EX_BINOP); add->binop.op=OP_ADD; add->binop.lhs=v2; add->binop.rhs=step_expr;
            s->forst.step_rhs=add;
            s->forst.body=body; s->forst.body_len=blen;
        } else {
            /* Implicit start=0: L var , end { } */
            expect(p,TK_COMMA);
            Expr *end=parse_expr(p);
            expect(p,TK_LBRACE);
            size_t blen=0; Stmt **body=parse_stmt_list(p,TK_RBRACE,&blen); expect(p,TK_RBRACE);

            Expr *zero=new_expr(p,EX_INTLIT); zero->ival=0;
            s->forst.has_init_decl=true;
            s->forst.init_name=vname.text; s->forst.init_typ=loop_type; s->forst.init_init=zero;

            Expr *cond_e=new_expr(p,EX_BINOP); cond_e->binop.op=OP_LT;
            Expr *vid=new_expr(p,EX_IDENT); vid->ident=vname.text;
            cond_e->binop.lhs=vid; cond_e->binop.rhs=end;
            s->forst.cond=cond_e;

            s->forst.has_step_assign=true; s->forst.step_name=vname.text;
            Expr *v2=new_expr(p,EX_IDENT); v2->ident=vname.text;
            Expr *one=new_expr(p,EX_INTLIT); one->ival=1;
            Expr *add=new_expr(p,EX_BINOP); add->binop.op=OP_ADD; add->binop.lhs=v2; add->binop.rhs=one;
            s->forst.step_rhs=add;
            s->forst.body=body; s->forst.body_len=blen;
        }
        break;
    }

    /* ~ expr { body }  — while loop (statement position) */
    case TK_TILDE: {
        advance(p); Expr *cond=parse_expr(p);
        expect(p,TK_LBRACE);
        size_t blen=0; Stmt **body=parse_stmt_list(p,TK_RBRACE,&blen); expect(p,TK_RBRACE);
        s->kind=ST_WHILE; s->whilest.cond=cond; s->whilest.body=body; s->whilest.body_len=blen;
        break;
    }

    /* ! name = expr ;  — pointer deref assign (and !name OP= expr compound) */
    case TK_BANG: {
        advance(p); Token name=expect(p,TK_IDENT);
        BinOp cop;
        if(compound_op(p,&cop)) {
            Expr *rhs=parse_expr(p); match(p,TK_SEMI);
            Expr *load=new_expr(p,EX_UNOP); load->unop.op=UOP_DEREF;
            Expr *id=new_expr(p,EX_IDENT); id->ident=name.text; load->unop.operand=id;
            Expr *comb=new_expr(p,EX_BINOP); comb->binop.op=cop; comb->binop.lhs=load; comb->binop.rhs=rhs;
            s->kind=ST_DEREFASSIGN; s->derefassign.name=name.text; s->derefassign.rhs=comb;
            break;
        }
        expect(p,TK_EQ);
        Expr *rhs=parse_expr(p); match(p,TK_SEMI);
        s->kind=ST_DEREFASSIGN; s->derefassign.name=name.text; s->derefassign.rhs=rhs;
        break;
    }

    /* IDENT ... — multiple cases:
     *   name := expr          — type-inferred declaration
     *   name : type = expr    — typed declaration
     *   name = expr           — assignment
     *   name [ expr ] = expr  — indexed assignment
     *   name ( args )         — call expression stmt
     */
    case TK_IDENT: {
        size_t saved=p->pos;
        Token nm=advance(p);

        /* := type-inferred declaration */
        if(check(p,TK_COLONEQ)) {
            advance(p); Expr *init=parse_expr(p); match(p,TK_SEMI);
            s->kind=ST_VARDECL; s->vardecl.name=nm.text;
            s->vardecl.typ=NULL; /* NULL = infer from init */
            s->vardecl.init=init; break;
        }

        /* : type [= expr] — typed declaration; initializer optional.
         *   x:[4096]1     -> uninitialized buffer (alloca only, no fill)
         *   x:3=7         -> initialized */
        if(check(p,TK_COLON)) {
            advance(p); Type *typ=parse_type(p);
            Expr *init=NULL;
            if(check(p,TK_EQ)){ advance(p); init=parse_expr(p); }
            match(p,TK_SEMI);
            s->kind=ST_VARDECL; s->vardecl.name=nm.text; s->vardecl.typ=typ; s->vardecl.init=init; break;
        }

        /* var . field [OP]= expr — struct field assignment (only when an
         * assignment follows; otherwise it's a field-read expression stmt). */
        if(check(p,TK_DOT)) {
            advance(p); Token f=expect(p,TK_IDENT);
            BinOp cop;
            if(compound_op(p,&cop)) {
                Expr *rhs=parse_expr(p); match(p,TK_SEMI);
                Expr *load=new_expr(p,EX_FIELD); load->field.var=nm.text; load->field.field=f.text;
                Expr *comb=new_expr(p,EX_BINOP); comb->binop.op=cop; comb->binop.lhs=load; comb->binop.rhs=rhs;
                s->kind=ST_FIELDASSIGN; s->fieldassign.var=nm.text; s->fieldassign.field=f.text; s->fieldassign.rhs=comb; break;
            }
            if(check(p,TK_EQ)) {
                advance(p); Expr *rhs=parse_expr(p); match(p,TK_SEMI);
                s->kind=ST_FIELDASSIGN; s->fieldassign.var=nm.text; s->fieldassign.field=f.text; s->fieldassign.rhs=rhs; break;
            }
            /* not an assignment — field read used in an expression statement */
            p->pos=saved;
            Expr *e=parse_expr(p); match(p,TK_SEMI);
            s->kind=ST_EXPRSTMT; s->exprstmt=e; break;
        }

        /* name OP= expr — compound assignment -> name = name OP expr */
        { BinOp cop;
          if(compound_op(p,&cop)) {
            Expr *rhs=parse_expr(p); match(p,TK_SEMI);
            Expr *lhs=new_expr(p,EX_IDENT); lhs->ident=nm.text;
            Expr *comb=new_expr(p,EX_BINOP); comb->binop.op=cop; comb->binop.lhs=lhs; comb->binop.rhs=rhs;
            s->kind=ST_ASSIGN; s->assign.name=nm.text; s->assign.rhs=comb; break;
          }
        }

        /* = expr — simple assignment */
        if(check(p,TK_EQ)) {
            advance(p); Expr *rhs=parse_expr(p); match(p,TK_SEMI);
            s->kind=ST_ASSIGN; s->assign.name=nm.text; s->assign.rhs=rhs; break;
        }

        /* [ expr ] = expr — indexed assignment (only if '=' follows the ]) */
        if(check(p,TK_LBRACKET)) {
            advance(p); Expr *idx=parse_expr(p); expect(p,TK_RBRACKET);
            if(check(p,TK_EQ)) {
                advance(p); Expr *rhs=parse_expr(p); match(p,TK_SEMI);
                s->kind=ST_IDXASSIGN; s->idxassign.name=nm.text; s->idxassign.idx=idx; s->idxassign.rhs=rhs; break;
            }
            /* a[i] OP= expr -> a[i] = a[i] OP expr (idx evaluated twice) */
            { BinOp cop;
              if(compound_op(p,&cop)) {
                Expr *rhs=parse_expr(p); match(p,TK_SEMI);
                Expr *load=new_expr(p,EX_ARRAYIDX); load->arridx.name=nm.text; load->arridx.idx=idx;
                Expr *comb=new_expr(p,EX_BINOP); comb->binop.op=cop; comb->binop.lhs=load; comb->binop.rhs=rhs;
                s->kind=ST_IDXASSIGN; s->idxassign.name=nm.text; s->idxassign.idx=idx; s->idxassign.rhs=comb; break;
              }
            }
            /* Not an assignment — it's an index expression statement.
             * Backtrack and parse the whole thing as an expression. */
            p->pos=saved;
            Expr *e=parse_expr(p); match(p,TK_SEMI);
            s->kind=ST_EXPRSTMT; s->exprstmt=e; break;
        }

        /* Otherwise: expression statement (e.g. function call) */
        p->pos=saved;
        Expr *e=parse_expr(p); match(p,TK_SEMI);
        s->kind=ST_EXPRSTMT; s->exprstmt=e; break;
    }

    default: {
        /* Expression statement (ternary, call, etc.) */
        if(check(p,TK_RBRACE)||check(p,TK_EOF)) {
            /* Empty / end-of-block: return a null stmt we'll skip */
            s->kind=ST_EXPRSTMT; s->exprstmt=NULL; break;
        }
        Expr *e=parse_expr(p); match(p,TK_SEMI);
        s->kind=ST_EXPRSTMT; s->exprstmt=e; break;
    }
    }
    return s;
}

/* ---- Function & Program parsing ----
 * v3: F name ( param* ) rettype { body }
 * Implicit return: if last stmt in body is ST_EXPRSTMT and rettype != void,
 *   convert it to ST_RETURN.
 */
static FuncDef parse_funcdef(Parser *p) {
    /* Consume # function sigil */
    if(!check(p,TK_HASH)){
        Token t=peek(p);
        fprintf(stderr,"Expected '#' for function def at %u:%u, got %s '%s'\n",
            t.line,t.col,token_kind_name(t.kind),t.text); exit(1);
    }
    advance(p);
    Token name=expect(p,TK_IDENT);
    expect(p,TK_LPAREN);

    DYNARRAY(char*)  pnames={0};
    DYNARRAY(Type*)  ptypes={0};

    /* Param forms (densest-first; default type is i32):
     *   a        -> a : i32        (bare name, the common case)
     *   *a       -> a : *i32       (pointer-to-i32, prefix star)
     *   a:T      -> a : T          (explicit type for anything else)
     */
    while(!check(p,TK_RPAREN)&&!check(p,TK_EOF)) {
        int is_ptr = 0;
        if (check(p,TK_STAR)) { advance(p); is_ptr = 1; }
        Token pn=expect(p,TK_IDENT);
        Type *pt;
        if (check(p,TK_COLON)) {
            advance(p);
            pt=parse_type(p);
            if (is_ptr) { Type *pp=(Type*)arena_alloc(p->a,sizeof(Type)); memset(pp,0,sizeof(*pp)); pp->kind=TY_PTR; pp->inner=pt; pt=pp; }
        } else {
            pt=(Type*)arena_alloc(p->a,sizeof(Type)); memset(pt,0,sizeof(*pt));
            if (is_ptr) { pt->kind=TY_PTR; Type *in=(Type*)arena_alloc(p->a,sizeof(Type)); memset(in,0,sizeof(*in)); in->kind=TY_I32; pt->inner=in; }
            else pt->kind=TY_I32;  /* default scalar type */
        }
        DA_PUSH(&pnames,pn.text,p->a);
        DA_PUSH(&ptypes,pt,p->a);
        match(p,TK_COMMA);
    }
    expect(p,TK_RPAREN);
    /* Return type defaults to i32. An explicit type is introduced by ':'
     *   #f(a) ...     -> returns i32 (common case, zero annotation)
     *   #f(a):6 ...   -> returns i64
     *   #f(a):v{...}  -> returns void
     */
    Type *ret;
    if (check(p,TK_COLON)) {
        advance(p);
        ret=parse_type(p);
    } else {
        ret=(Type*)arena_alloc(p->a,sizeof(Type)); memset(ret,0,sizeof(*ret));
        ret->kind=TY_I32;
    }

    /* Body: either a single expression (implicit return) or { stmts } */
    size_t body_len=0; Stmt **body=NULL;
    if(check(p,TK_LBRACE)) {
        advance(p);
        body=parse_stmt_list(p,TK_RBRACE,&body_len);
        expect(p,TK_RBRACE);
    } else {
        /* Single-expression body: wrap as implicit return */
        Expr *e=parse_expr(p); match(p,TK_SEMI);
        Stmt *s=(Stmt*)arena_alloc(p->a,sizeof(Stmt)); memset(s,0,sizeof(*s));
        s->kind=ST_RETURN; s->ret.val=e;
        Stmt **arr=(Stmt**)arena_alloc(p->a,sizeof(Stmt*)); arr[0]=s;
        body=arr; body_len=1;
    }

    /* Implicit return: if last stmt is an expression and return type is not void */
    if(body_len>0 && ret->kind!=TY_VOID) {
        Stmt *last=body[body_len-1];
        if(last->kind==ST_EXPRSTMT && last->exprstmt!=NULL) {
            last->kind=ST_RETURN;
            last->ret.val=last->exprstmt;
        }
    }

    FuncDef f;
    f.name=name.text;
    f.param_names=pnames.data; f.param_types=ptypes.data; f.param_count=pnames.len;
    f.ret_type=ret; f.body=body; f.body_len=body_len;
    return f;
}

/* ---- Struct definition parsing ----
 * $Name{ field:type field:type ... }   (commas optional)
 */
static StructDef parse_structdef(Parser *p) {
    expect(p,TK_DOLLAR);
    Token name=expect(p,TK_IDENT);
    /* register the name immediately so fields/later code can reference it */
    p->struct_names=(char**)realloc(p->struct_names,(p->struct_name_count+1)*sizeof(char*));
    p->struct_names[p->struct_name_count++]=name.text;
    expect(p,TK_LBRACE);
    DYNARRAY(char*) fnames={0};
    DYNARRAY(Type*) ftypes={0};
    while(!check(p,TK_RBRACE)&&!check(p,TK_EOF)) {
        while(check(p,TK_SEMI)||check(p,TK_COMMA)) advance(p);
        if(check(p,TK_RBRACE)) break;
        Token fn=expect(p,TK_IDENT);
        expect(p,TK_COLON);
        Type *ft=parse_type(p);
        DA_PUSH(&fnames,fn.text,p->a);
        DA_PUSH(&ftypes,ft,p->a);
        while(check(p,TK_SEMI)||check(p,TK_COMMA)) advance(p);
    }
    expect(p,TK_RBRACE);
    StructDef sd; sd.name=name.text;
    sd.field_names=fnames.data; sd.field_types=ftypes.data; sd.field_count=fnames.len;
    return sd;
}

Program parse(TokenArray tokens, Arena *a) {
    Parser p={.tokens=tokens.data,.len=tokens.len,.pos=0,.a=a,
              .struct_names=NULL,.struct_name_count=0};
    DYNARRAY(FuncDef) funcs={0};
    DYNARRAY(StructDef) structs={0};
    while(!check(&p,TK_EOF)){
        /* Skip stray semicolons at top level */
        while(check(&p,TK_SEMI)) advance(&p);
        if(check(&p,TK_EOF)) break;
        if(check(&p,TK_DOLLAR)) {
            StructDef sd=parse_structdef(&p);
            DA_PUSH(&structs,sd,&p);
            continue;
        }
        FuncDef f=parse_funcdef(&p);
        DA_PUSH(&funcs,f,&p);
    }
    Program prog;
    prog.funcs=funcs.data; prog.func_count=funcs.len;
    prog.structs=structs.data; prog.struct_count=structs.len;
    return prog;
}
