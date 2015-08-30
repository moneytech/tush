/*For asprintf*/
#define _GNU_SOURCE

#include "type.h"
#include "type-internal.h"

#include <vector.h>
#include <hashmap.h>

#include "common.h"

/*==== Type ctors and dtors ====*/

static type* typeCreate (typeKind kind, type init) {
    precond(kind != type_KindNo);

    type* dt = malloci(sizeof(type), &init);
    dt->kind = kind;
    return dt;
}

static void typeDestroy (type* dt) {
    if (dt->kind == type_Tuple)
        vectorFree(&dt->types);

    else if (dt->kind == type_Forall)
        vectorFree(&dt->typevars);

    free(dt->str);
    free(dt);
}

type* typeUnitary (typeSys* ts, typeKind kind) {
    precond(!typeKindIsntUnitary(kind));

    /*Only allocate one struct per unitary type*/
    if (!ts->unitaries[kind])
        ts->unitaries[kind] = typeCreate(kind, (type) {});

    return ts->unitaries[kind];
}

type* typeInt (typeSys* ts) {
    return typeUnitary(ts, type_Int);
}

type* typeFile (typeSys* ts) {
    return typeUnitary(ts, type_File);
}

static type* typeNonUnitary (typeSys* ts, typeKind kind, type init) {
    type* dt = typeCreate(kind, init);
    vectorPush(&ts->others, dt);
    return dt;
}

type* typeFn (typeSys* ts, type* from, type* to) {
    return typeNonUnitary(ts, type_Fn, (type) {
        .from = from, .to = to
    });
}

type* typeList (typeSys* ts, type* elements) {
    return typeNonUnitary(ts, type_List, (type) {
        .elements = elements
    });
}

type* typeTuple (typeSys* ts, vector(type*) types) {
    return typeNonUnitary(ts, type_Tuple, (type) {
        .types = types
    });
}

type* typeVar (typeSys* ts) {
    return typeNonUnitary(ts, type_Var, (type) {});
}

type* typeForall (typeSys* ts, vector(type*) typevars, type* dt) {
    return typeNonUnitary(ts, type_Forall, (type) {
        .typevars = typevars, .dt = dt
    });
}

type* typeInvalid (typeSys* ts) {
    return typeUnitary(ts, type_Invalid);
}

/*==== Type system ====*/

typeSys typesInit (void) {
    return (typeSys) {
        .unitaries = {},
        .others = vectorInit(100, malloc)
    };
}

typeSys* typesFree (typeSys* ts) {
    for (unsigned int i = 0; i < sizeof(ts->unitaries)/sizeof(*ts->unitaries); i++)
        if (ts->unitaries[i])
            typeDestroy(ts->unitaries[i]);

    vectorFreeObjs(&ts->others, (vectorDtor) typeDestroy);

    return ts;
}

/*==== ====*/

type* typeFnChain (int kindNo, typeSys* ts, ...) {
    type* result = typeUnitary(ts, type_Unit);

    va_list args;
    va_start(args, ts);

    for (int i = 0; i < kindNo; i++) {
        typeKind kind = va_arg(args, int);
        type* dt = typeUnitary(ts, kind);

        /*On the first iteration the result is just the unitary type
          (using the induction variable to make it easy for the compiler to optimize)*/
        if (i == 0)
            result = dt;

        else
            result = typeFn(ts, result, dt);
    }

    va_end(args);

    return result;
}

type* typeTupleChain (int arity, typeSys* ts, ...) {
    va_list args;
    va_start(args, ts);

    vector(type*) types = vectorInit(arity, malloc);

    /*Turn the arg list into a vector*/
    for (int i = 0; i < arity; i++) {
        type* dt = va_arg(args, type*);
        vectorPush(&types, dt);
    }

    va_end(args);

    return typeTuple(ts, types);
}

/*==== String representation ===*/

typedef struct strCtx {
    /*Use a vector because n is small?*/
    intmap(type*, const char*) typevars;
    int namesGiven;
} strCtx;

static const char* strNewTypevar (strCtx* ctx) {
    static const char* strs[10] = {"'a", "'b", "'c", "'d", "'e", "'f", "'g", "'h", "'i", "'j"};

    if (ctx->namesGiven >= 10) {
        errprintf("Run out of static strings to allocate\n");
        return "'";

    } else
        return strs[ctx->namesGiven++];
}

static const char* strMapTypevar (strCtx* ctx, const type* dt) {
    if (mapNull(ctx->typevars))
        ctx->typevars = intmapInit(32, malloc);

    const char* str = intmapMap(&ctx->typevars, (intptr_t) dt);

    /*Get a new string if the typevar isn't already mapped*/
    if (!str) {
        str = strNewTypevar(ctx);
        intmapAdd(&ctx->typevars, (intptr_t) dt, (void*) str);
    }

    return str;
}

static const char* typeGetStrImpl (strCtx* ctx, type* dt, bool firstLevel) {
    if (dt->str)
        return dt->str;

    switch (dt->kind) {
    case type_Unit: return "()";
    case type_Int: return "Int";
    case type_Num: return "Num";
    case type_Bool: return "Bool";
    case type_Str: return "Str";
    case type_File: return "File";
    case type_Invalid: return "<invalid>";
    case type_KindNo: return "<KindNo, not real>";

    case type_Fn: {
        const char *from = typeGetStrImpl(ctx, dt->from, false),
                   *to = typeGetStrImpl(ctx, dt->to, false);

        bool higherOrderFn = typeIsFn(dt->from) || dt->from->kind == type_Forall;

        bool allocSuccess = 0 != asprintf(&dt->str, higherOrderFn ? "(%s) -> %s" : "%s -> %s", from, to);

        if (!precond(allocSuccess))
            return " -> ";

        return dt->str;
    }

    case type_List: {
        const char* elements = typeGetStrImpl(ctx, dt->elements, false);

        bool allocSuccess = 0 != asprintf(&dt->str, "[%s]", elements);

        if (!precond(allocSuccess))
            return "[ ]";

        return dt->str;
    }

    case type_Tuple: {
        /*Note: VLA*/
        const char* typeStrs[dt->types.length];
        int length = 0;

        /*Work out the length of all the subtypes and store their strings*/
        for_vector_indexed (i, type* dt, dt->types, {
            typeStrs[i] = typeGetStrImpl(ctx, dt, false);
            length += strlen(typeStrs[i]) + 2;
        })

        /*Print the subtypes in parentheses, with comma separators*/

        length += 3;
        dt->str = malloc(length);
        strcpy(dt->str, "(");

        size_t pos = 1;
        pos += strcatwith(dt->str+pos, dt->types.length, (char**) typeStrs, ", ");

        strcpy(dt->str+pos, ")");

        return dt->str;
    }

    case type_Var:
        return strMapTypevar(ctx, dt);

    case type_Forall: {
        const char* dtStr = typeGetStrImpl(ctx, dt->dt, false);

        bool higherKinded = !firstLevel;

        if (!higherKinded)
            return dtStr;

        /*Note: VLA*/
        const char* typevarStrs[dt->typevars.length];

        for_vector_indexed (i, type* typevar, dt->typevars, {
            typevarStrs[i] = strMapTypevar(ctx, typevar);
        })

        char* typevarStr = strjoinwith(dt->typevars.length, (char**) typevarStrs, ", ", malloc);

        bool allocSuccess = 0 != asprintf(&dt->str, "forall %s. %s", typevarStr, dtStr);
        free(typevarStr);

        if (!precond(allocSuccess))
            /*Could return dtStr, but best not to give valid /looking/
              output if it isn't actually valid.*/
            return "(forall . )";

        return dt->str;
    }}

    return "<unhandled type kind>";
}

strCtx strCtxInit () {
    return (strCtx) {
        /*Avoid a malloc unless necessary*/
        .typevars = {},
        .namesGiven = 0
    };
}

strCtx* strCtxFree (strCtx* ctx) {
    intmapFree(&ctx->typevars);
    return ctx;
}

const char* typeGetStr (const type* dt) {
	/*The context keeps track of typevars already given names
	  and which names have been given.*/
    strCtx ctx = strCtxInit();
    const char* str = typeGetStrImpl(&ctx, (type*) dt, true);
    strCtxFree(&ctx);

    return str;
}

/*==== Tests ====*/

static void seeThroughQuantifier (const type** dt) {
    if ((*dt)->kind == type_Forall)
        *dt = (*dt)->dt;
}

/*Returns whether the type is logically of a kind
  There may be indirection through quantifiers.
  Do not use this if you will access the structure of the type.*/
bool typeIsKind (typeKind kind, const type* dt) {
    seeThroughQuantifier(&dt);
    return dt->kind == kind;
}

bool typeIsInvalid (const type* dt) {
    return typeIsKind(type_Invalid, dt);
}

bool typeIsFn (const type* dt) {
    return typeIsKind(type_Fn, dt);
}

bool typeIsList (const type* dt) {
    return typeIsKind(type_List, dt);
}

bool typeIsEqual (const type* l, const type* r) {
    if (!precond(l) || !precond(r))
        return false;

    if (l == r)
        return true;

    else if (l->kind != r->kind)
        return false;

    else {
        switch (l->kind) {
        case type_Fn:
            return    typeIsEqual(l->from, r->from)
                   && typeIsEqual(l->to, r->to);

        case type_List:
            return typeIsEqual(l->elements, r->elements);

        case type_Tuple:
            if (l->types.length != r->types.length)
                return false;

            for (int i = 0; i < l->types.length; i++) {
                type *typeL = vectorGet(l->types, i),
                     *typeR = vectorGet(r->types, i);

                if (!typeIsEqual(typeL, typeR))
                    return false;
            }

            return true;

        default:
            errprintf("Unhandled type kind, %s\n", typeGetStr(l));
            return false;
            //todo other kinds
        }
    }
}

/*==== Operations ====*/

static type* fnGetTo (typeSys* ts, const type* fn) {
    if (!precond(typeIsFn(fn)))
        return 0;

    switch (fn->kind) {
    case type_Fn:
        return fn->to;

    case type_Forall:
        //todo: not all typevar will appear in the result, remove them
        return typeForall(ts, fn->typevars, fnGetTo(ts, fn->dt));

    default:
        errprintf("Unhandled function kind, %s\n", typeGetStr(fn));
        return 0;
    }
}

bool typeAppliesToFn (typeSys* ts, const type* arg, const type* fn, type** result) {
    if (!precond(arg) || !precond(fn))
        return false;

    bool applies;

    if (!typeIsFn(fn))
        return false;

    else if (fn->kind == type_Fn) {
        applies = typeIsEqual(fn->from, arg);

    /*The function is quantified, so find the types which satisfy this application*/
    } else if (fn->kind == type_Forall && fn->dt->kind == type_Fn) {
        fn = unifyArgWithFn(ts, arg, fn);
		applies = fn != 0;

    } else {
        errprintf("Unknown function representation, kind %d, %s\n", fn->kind, typeGetStr(fn));
        *result = typeInvalid(ts);
        return true;
    }

    if (applies && result)
        *result = fnGetTo(ts, fn);

    return applies;
}

bool typeUnitAppliesToFn (const type* fn, type** result) {
    bool applies = fn->kind == type_Fn && fn->from->kind == type_Unit;

    if (applies && result)
        *result = fn->to;

    return applies;
}

bool typeIsListOf (const type* dt, type** elements) {
    seeThroughQuantifier(&dt);

    if (dt->kind == type_List) {
        *elements = dt->elements;
        return true;

    } else
        return false;
}

bool typeIsTupleOf (const type* dt, vector(const type*)* types) {
    seeThroughQuantifier(&dt);

    if (dt->kind == type_Tuple) {
        *types = dt->types;
        return true;

    } else
        return false;
}

bool typeCanUnify (typeSys* ts, const type* l, const type* r, type** result) {
    *result = unifyMatching(ts, l, r);
    return *result != 0;
}
