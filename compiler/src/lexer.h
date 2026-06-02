#pragma once
#include "common.h"

typedef enum {
    /* v3 grammar sigils (collision-free with identifiers) */
    TK_HASH,       // #  — function definition
    TK_AT,         // @  — for loop
    /* while uses TK_TILDE in statement position */

    /* punctuation / operators */
    TK_QUESTION,   // ?
    TK_COLON,      // :
    TK_BACKSLASH,  // \  — return statement
    TK_BANG,       // !  — pointer deref (unary)
    TK_AMP,        // &  — address-of (unary) / bitwise AND (binary)
    TK_CARET,      // ^  — XOR
    TK_TILDE,      // ~  — bitwise NOT (unary)
    TK_PERCENT,    // %  — modulo
    TK_PIPE,       // |  — bitwise OR
    TK_GT2,        // >> — right shift
    TK_LSHIFT,     // << — left shift

    TK_LPAREN,     // (
    TK_RPAREN,     // )
    TK_LBRACE,     // {
    TK_RBRACE,     // }
    TK_LBRACKET,   // [
    TK_RBRACKET,   // ]
    TK_COMMA,      // ,
    TK_SEMI,       // ;

    TK_PLUS,       // +
    TK_MINUS,      // -
    TK_STAR,       // *  — multiply (expression) / pointer type (type context)
    TK_SLASH,      // /
    TK_EQ,         // =
    TK_EQEQ,       // ==
    TK_BANGEQ,     // !=
    TK_LT,         // <
    TK_GT,         // >
    TK_LTEQ,       // <=
    TK_GTEQ,       // >=
    TK_AMPAMP,     // &&
    TK_PIPEPIPE,   // ||
    TK_COLONEQ,    // :=  — type-inferred declaration

    TK_IDENT,
    TK_INTLIT,
    TK_FLOATLIT,
    TK_BOOLTRUE,   // T
    TK_STRLIT,     // "..."

    TK_EOF,
} TokenKind;

typedef struct {
    TokenKind kind;
    char     *text;
    uint32_t  line;
    uint32_t  col;
} Token;

typedef struct {
    Token  *data;
    size_t  len;
    size_t  cap;
} TokenArray;

TokenArray lex(const char *src, Arena *a);
const char *token_kind_name(TokenKind k);
