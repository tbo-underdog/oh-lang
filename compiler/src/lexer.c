#include "lexer.h"

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_hex(char c) { return is_digit(c) || (c>='a'&&c<='f') || (c>='A'&&c<='F'); }

const char *token_kind_name(TokenKind k) {
    switch(k) {
    case TK_HASH: return "#";
    case TK_AT: return "@";
    case TK_QUESTION: return "?";
    case TK_COLON: return ":";
    case TK_BACKSLASH: return "\\";
    case TK_BANG: return "!";
    case TK_AMP: return "&";
    case TK_CARET: return "^";
    case TK_TILDE: return "~";
    case TK_PERCENT: return "%";
    case TK_PIPE: return "|";
    case TK_GT2: return ">>";
    case TK_LSHIFT: return "<<";
    case TK_LPAREN: return "(";
    case TK_RPAREN: return ")";
    case TK_LBRACE: return "{";
    case TK_RBRACE: return "}";
    case TK_LBRACKET: return "[";
    case TK_RBRACKET: return "]";
    case TK_COMMA: return ",";
    case TK_SEMI: return ";";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_EQ: return "=";
    case TK_EQEQ: return "==";
    case TK_BANGEQ: return "!=";
    case TK_LT: return "<";
    case TK_GT: return ">";
    case TK_LTEQ: return "<=";
    case TK_GTEQ: return ">=";
    case TK_AMPAMP: return "&&";
    case TK_PIPEPIPE: return "||";
    case TK_COLONEQ: return ":=";
    case TK_DOT: return ".";
    case TK_DOLLAR: return "$";
    case TK_IDENT: return "IDENT";
    case TK_INTLIT: return "INT";
    case TK_FLOATLIT: return "FLOAT";
    case TK_BOOLTRUE: return "T";
    case TK_BOOLFALSE: return "F";
    case TK_STRLIT: return "STRLIT";
    case TK_EOF: return "EOF";
    default: return "?";
    }
}

TokenArray lex(const char *src, Arena *a) {
    TokenArray arr = {0};
    size_t pos = 0;
    uint32_t line = 1, col = 1;
    size_t srclen = strlen(src);

    while (1) {
        bool saw_newline = false;
        while (pos < srclen) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (c == '\n') { line++; col = 1; saw_newline = true; } else col++;
                pos++;
            } else if (c == '/' && pos+1 < srclen && src[pos+1] == '/') {
                while (pos < srclen && src[pos] != '\n') pos++;
            } else break;
        }

        /* Newline acts as a statement terminator. Emit a synthetic ';'
         * unless redundant (start of input, or previous token already a
         * separator / block opener). Parser dedups leading semicolons. */
        if (saw_newline && arr.len > 0) {
            TokenKind last = arr.data[arr.len-1].kind;
            if (last != TK_SEMI && last != TK_LBRACE && last != TK_COMMA) {
                Token sep = { .kind = TK_SEMI, .line = line, .col = col };
                sep.text = arena_strdup(a, ";", 1);
                DA_PUSH(&arr, sep, a);
            }
        }

        Token tok = { .line = line, .col = col };

        if (pos >= srclen) {
            tok.kind = TK_EOF;
            tok.text = arena_strdup(a, "", 0);
            DA_PUSH(&arr, tok, a);
            break;
        }

        char c = src[pos];
        size_t start = pos;

#define TWO(a1,a2,k) if (c == (a1) && pos+1 < srclen && src[pos+1] == (a2)) { \
    pos += 2; col += 2; tok.kind = (k); tok.text = arena_strdup(a, src+start, 2); \
    DA_PUSH(&arr, tok, a); continue; }

        TWO(':','=', TK_COLONEQ)
        TWO('>','>', TK_GT2)
        TWO('=','=', TK_EQEQ)
        TWO('!','=', TK_BANGEQ)
        TWO('<','=', TK_LTEQ)
        TWO('>','=', TK_GTEQ)
        TWO('&','&', TK_AMPAMP)
        TWO('|','|', TK_PIPEPIPE)
        TWO('<','<', TK_LSHIFT)
#undef TWO

        TokenKind sk = TK_EOF;
        bool single = true;
        switch(c) {
        case '#': sk = TK_HASH;     break;
        case '@': sk = TK_AT;       break;
        case '?': sk = TK_QUESTION; break;
        case ':': sk = TK_COLON;    break;
        case '\\':sk = TK_BACKSLASH;break;
        case '!': sk = TK_BANG;     break;
        case '&': sk = TK_AMP;      break;
        case '^': sk = TK_CARET;    break;
        case '~': sk = TK_TILDE;    break;
        case '%': sk = TK_PERCENT;  break;
        case '|': sk = TK_PIPE;     break;
        case '(': sk = TK_LPAREN;   break;
        case ')': sk = TK_RPAREN;   break;
        case '{': sk = TK_LBRACE;   break;
        case '}': sk = TK_RBRACE;   break;
        case '[': sk = TK_LBRACKET; break;
        case ']': sk = TK_RBRACKET; break;
        case ',': sk = TK_COMMA;    break;
        case ';': sk = TK_SEMI;     break;
        case '+': sk = TK_PLUS;     break;
        case '-': sk = TK_MINUS;    break;
        case '*': sk = TK_STAR;     break;
        case '/': sk = TK_SLASH;    break;
        case '=': sk = TK_EQ;       break;
        case '<': sk = TK_LT;       break;
        case '>': sk = TK_GT;       break;
        case '.': sk = TK_DOT;      break;
        case '$': sk = TK_DOLLAR;   break;
        default:  single = false;   break;
        }
        if (single) {
            pos++; col++;
            tok.kind = sk;
            tok.text = arena_strdup(a, src+start, 1);
            DA_PUSH(&arr, tok, a);
            continue;
        }

        /* string literal */
        if (c == '"') {
            pos++; col++;
            size_t str_start = pos;
            while (pos < srclen && src[pos] != '"') {
                if (src[pos] == '\\') { pos++; col++; }
                pos++; col++;
            }
            size_t str_len = pos - str_start;
            char *raw = arena_strdup(a, src+str_start, str_len);
            if (pos < srclen) { pos++; col++; }
            tok.kind = TK_STRLIT;
            tok.text = raw;
            DA_PUSH(&arr, tok, a);
            continue;
        }

        /* identifier or keyword */
        if (is_ident_start(c)) {
            while (pos < srclen && is_ident_cont(src[pos])) { pos++; col++; }
            size_t len = pos - start;
            char *text = arena_strdup(a, src+start, len);
            if      (len == 1 && text[0] == 'T') tok.kind = TK_BOOLTRUE;
            else if (len == 1 && text[0] == 'F') tok.kind = TK_BOOLFALSE;
            else                                 tok.kind = TK_IDENT;
            tok.text = text;
            DA_PUSH(&arr, tok, a);
            continue;
        }

        /* number */
        if (is_digit(c)) {
            bool is_float = false;
            if (c == '0' && pos+1 < srclen && (src[pos+1]=='x' || src[pos+1]=='X')) {
                pos += 2; col += 2;                       /* hex literal: 0x... */
                while (pos < srclen && is_hex(src[pos])) { pos++; col++; }
                tok.kind = TK_INTLIT;
                tok.text = arena_strdup(a, src+start, pos-start);
                DA_PUSH(&arr, tok, a);
                continue;
            }
            while (pos < srclen && is_digit(src[pos])) { pos++; col++; }
            if (pos < srclen && src[pos] == '.') {
                is_float = true;
                pos++; col++;
                while (pos < srclen && is_digit(src[pos])) { pos++; col++; }
            }
            size_t len = pos - start;
            tok.kind = is_float ? TK_FLOATLIT : TK_INTLIT;
            tok.text = arena_strdup(a, src+start, len);
            DA_PUSH(&arr, tok, a);
            continue;
        }

        fprintf(stderr, "Lex error: unexpected char '%c' at %u:%u\n", c, line, col);
        exit(1);
    }

    return arr;
}
