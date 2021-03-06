#include "sym.h"

#include <gc.h>
#include "common.h"

static void symAddChild(sym* parent, sym* child) {
    if (!precond(parent) || !precond(child) || !precond(child->parent == 0))
        return;

    vectorPush(&parent->children, child);
    child->parent = parent;
}

static sym* symCreate (symKind kind, const char* name, sym init) {
    /*A sym can contain GC references, so the GC needs to know of it*/
    sym* symbol = GC_MALLOC_UNCOLLECTABLE(sizeof(sym));
    *symbol = init;
    symbol->kind = kind;
    symbol->name = strdup(name);
    return symbol;
}

static sym* symCreateParented (symKind kind, sym* parent, const char* name, sym init) {
    sym* symbol = symCreate(kind, name, init);
    symAddChild(parent, symbol);
    return symbol;
}

static void symDestroy (sym* symbol) {
    vectorFreeObjs(&symbol->children, (vectorDtor) symDestroy);
    free(symbol->name);
    GC_FREE(symbol);
}

sym* symAdd (sym* parent, const char* name) {
    return symCreateParented(symNormal, parent, name, (sym) {});
}

sym* symAddScope (sym* parent) {
    return symCreateParented(symScope, parent, "<scope>", (sym) {
        .children = vectorInit(10, malloc)
    });
}

sym* symInit (void) {
    return symCreate(symScope, "<global scope>", (sym) {
        .children = vectorInit(50, malloc)
    });
}

void symEnd (sym* global) {
    symDestroy(global);
}

const char* symGetName (const sym* symbol) {
    return   !symbol ? "<no symbol attached>"
           : !symbol->name ? "<unnamed symbol>"
           : symbol->name;
}

sym* symLookup (const sym* scope, const char* name) {
    /*Look backwards, for the most recent definition*/
    for_vector_reverse (sym* symbol, scope->children, {
        if (!strcmp(name, symbol->name))
            return symbol;
    })

    if (scope->parent)
        return symLookup(scope->parent, name);

    return 0;
}

bool symIsInside (const sym* look, const sym* scope) {
    if (look->parent == scope)
        return true;

    for_vector (sym* child, scope->children, {
        if (child->kind == symScope) {
            if (symIsInside(look, child))
                return true;
        }
    })

    return false;
}
