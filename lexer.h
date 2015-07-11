#pragma once

#include <stdlib.h>
#include <ctype.h>

#include "common.h"
#include "token.h"

/*TODO: unicode*/

enum {
    lexerDefaultBufSize = 128
};

typedef struct lexerCtx {
    const char* input;
    int pos;

    /*The current token being lexed*/
    char* buffer;
    int bufsize;
    int length;
} lexerCtx;

static lexerCtx lexerInit (const char* str);
static lexerCtx* lexerDestroy (lexerCtx* ctx);

static bool lexerEOF (lexerCtx* ctx);
static token lexerNext (lexerCtx* ctx);

/*==== Inline implementations ====*/

inline static lexerCtx lexerInit (const char* str) {
    return (lexerCtx) {
        .input = str,
        .pos = 0,

        .buffer = calloc(lexerDefaultBufSize, 1),
        .bufsize = lexerDefaultBufSize,
        .length = 0
    };
}

inline static lexerCtx* lexerDestroy (lexerCtx* ctx) {
    free(ctx->buffer);
    return ctx;
}

inline static char lexerCurrent (lexerCtx* ctx) {
    return ctx->input[ctx->pos];
}

inline static bool lexerEOF (lexerCtx* ctx) {
    return lexerCurrent(ctx) == 0;
}

inline static void lexerSkip (lexerCtx* ctx) {
    if (!lexerEOF(ctx))
        ctx->pos++;
}

inline static void lexerEat (lexerCtx* ctx) {
    //todo resize
    ctx->buffer[ctx->length++] = lexerCurrent(ctx);
    lexerSkip(ctx);
}

inline static token lexerNext (lexerCtx* ctx) {
    /*Skip whitespace*/
    while (isspace(lexerCurrent(ctx)))
        lexerSkip(ctx);

    if (lexerEOF(ctx))
        return tokenMakeEOF();

    ctx->length = 0;

    token tok = {
        .kind = tokenNormal,
        .buffer = ctx->buffer
    };

    switch (lexerCurrent(ctx)) {
    /*String or character literal*/
    case '"': case '\'': {
        tok.kind = lexerCurrent(ctx) == '"' ? tokenLitStr : tokenLitChar;

        char quote = lexerCurrent(ctx);
        lexerEat(ctx);

        while (lexerCurrent(ctx) != quote) {
            if (lexerCurrent(ctx) == '\\')
                lexerEat(ctx);

            lexerEat(ctx);
        }

        lexerEat(ctx);
        break;
    }

    /*Delimiter op*/
    case '(': case ')':
    case '[': case ']':
    case '{': case '}':
    case ',': case '`':
        tok.kind = tokenOp;
        lexerEat(ctx);

    break;
    /*"Word"*/
    default: {
        bool exit = false;

        do {
            lexerEat(ctx);

            switch (lexerCurrent(ctx)) {
            case '"': case '\'':
            case '(': case ')':
            case '[': case ']':
            case '{': case '}':
            case ',': case '`':
            case '\n': case '\r':
            case '\t': case ' ':
                exit = true;
            }
        } while (!exit && !lexerEOF(ctx));
    }}

    ctx->buffer[ctx->length++] = 0;

    return tok;
};