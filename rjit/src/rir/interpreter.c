#include "interpreter.h"

// GNU-R stuff we need

extern SEXP R_TrueValue;
extern SEXP R_FalseValue;
extern SEXP Rf_NewEnvironment(SEXP, SEXP, SEXP);
extern Rboolean R_Visible;
extern SEXP forcePromise(SEXP);



// TODO force inlinine for clang & gcc
#define INLINE __attribute__((always_inline)) inline

// helpers

/** How many bytes do we need to align on 4 byte boundary?
 */
unsigned pad4(unsigned sizeInBytes) {
    unsigned x = sizeInBytes % 4;
    return (x != 0) ? (sizeInBytes + 4 - x) : sizeInBytes;
}


// Code object --------------------------------------------------------------------------------------------------------

OpcodeT* code(Code* c) {
    return (OpcodeT*)c->data;
}

unsigned* src(Code* c) {
    return (unsigned*)(c->data + pad4(c->codeSize));
}

Function* function(Code* c) {
    return (Function*)(c - c->header);
}

Code* next(Code* c) {
    return (Code*)(c->data + pad4(c->codeSize) + c->srcLength);
}

// Function -----------------------------------------------------------------------------------------------------------

bool isValidFunction(SEXP s) {
    if (TYPEOF(s) != INTSXP)
        return false;
    // TODO check magicVersion
}

Function* origin(Function* f) {
    // TODO
}

Code* begin(Function* f) {
    return f->data;
}

Code* end(Function* f) {
    return (Code*)((uint8_t*)f->data + f->size);
}

Code * codeAt(Function * f, unsigned offset) {
    return (Code*)((uint8_t*)f + offset);
}

// Runtime support ----------------------------------------------------------------------------------------------------

/** SEXP stack
 */
typedef struct {
    SEXP* stack;
    size_t length;
    size_t capacity;
} Stack;

Stack stack_;

INLINE bool stackEmpty() {
    return stack_.length == 0;
}

INLINE SEXP at(unsigned index) {
    return stack_.stack[index];
}

INLINE SEXP pop() {
    return stack_.stack[--stack_.length];
}

INLINE SEXP top() {
    return stack_.stack[stack_.length];
}

INLINE void push(SEXP val) {
    stack_.stack[stack_.length++] = val;
}

void checkStackSize(unsigned minFree) {
    unsigned newCap = stack_.capacity;
    while (stack_.length + minFree < newCap)
        newCap *= 2;
    if (newCap != stack_.capacity) {
        SEXP * newStack = malloc(newCap * sizeof(SEXP));
        memcpy(newStack, stack_.stack, stack_.length * sizeof(SEXP));
        free(stack_.stack);
        stack_.stack = newStack;
        stack_.capacity = newCap;
    }
}

/** Unboxed integer stack

  (separated because of the gc, should be merged in stack later on)
 */
typedef struct {
    int* stack;
    size_t length;
    size_t capacity;

} iStack;

iStack istack_;

INLINE bool iStackEmpty() {
    return istack_.length == 0;
}

INLINE int iPop() {
    return istack_.stack[--istack_.length];
}

INLINE int iTop() {
    return istack_.stack[istack_.length];
}

INLINE void iPush(int val) {
    istack_.stack[istack_.length++] = val;
}

void iCheckStackSize(unsigned minFree) {
    unsigned newCap = istack_.capacity;
    while (istack_.length + minFree < newCap)
        newCap *= 2;
    if (newCap != istack_.capacity) {
        int * newStack = malloc(newCap * sizeof(int));
        memcpy(newStack, istack_.stack, istack_.length * sizeof(int));
        free(istack_.stack);
        istack_.stack = newStack;
        istack_.capacity = newCap;
    }
}

/** Constant and ast pools

 */

typedef struct {
    SEXP pool;
    size_t length;
    size_t capacity;
} Pool;

Pool createPool(size_t capacity) {
    Pool result;
    result.length = 0;
    result.capacity = capacity;
    result.pool = Rf_allocVector(VECSXP, capacity);
    // add to precious list
    //Precious::add(result.pool);
}

Pool cp_;
Pool src_;

// grow the size of the pool
void grow(Pool * p) {
    size_t tmp = p->capacity;
    // grow capacity 2 times
    p->capacity = tmp * 2;

    // allocate new pool
    SEXP temp = Rf_allocVector(VECSXP, p->capacity);
    //Precious::add(temp);

    // transfer values over
    for (size_t i = 0; i <= tmp; ++i){
        SET_VECTOR_ELT(temp, i, VECTOR_ELT(p->pool, i));
    }

    // remove the old pool
    //Precious::remove(p->pool);
    p->pool = temp;

    // add the new pool, and remove the temp pool
    //Precious::add(p->pool);
    //Precious::remove(temp);
}

INLINE SEXP constant(size_t index) {
    return VECTOR_ELT(cp_.pool, index);
}

INLINE size_t addConstant(SEXP value) {

    // check the capacity of the constant pool
    if(cp_.capacity <= cp_.length){
        grow(&cp_);
    } 

    // allocate value into the constant pool
    SET_VECTOR_ELT(cp_.pool, cp_.length, value);
    cp_.length = cp_.length + 1;
    
    // return the position length? 
    return cp_.length;
}



/** AST (source) pool

 */

INLINE SEXP source(size_t index) {
    return VECTOR_ELT(src_.pool, index);
}

INLINE size_t addSource(SEXP value) {
    // check the capacity of the constant pool
    if(src_.capacity <= src_.length){
        grow(&src_);
    } 

    // allocate value into the constant pool
    SET_VECTOR_ELT(src_.pool, src_.length, value);
    src_.length = src_.length + 1;
    
    // return the position length? 
    return src_.length;
}

// bytecode accesses


INLINE Opcode readOpcode(OpcodeT** pc) {
    Opcode result = (Opcode)(**pc);
    *pc += sizeof(OpcodeT);
    return result;
}

INLINE unsigned readImmediate(OpcodeT** pc) {
    unsigned result = (Immediate)(**pc);
    *pc += sizeof(Immediate);
    return result;
}

INLINE SEXP readConst(OpcodeT ** pc) {
    return constant(readImmediate(pc));
}

INLINE int readJumpOffset(OpcodeT** pc) {
    int result = (JumpOffset)(**pc);
    *pc += sizeof(JumpOffset);
    return result;
}








// TODO check if there is a function for this in R
INLINE SEXP promiseValue(SEXP promise) {
    // TODO if already evaluated, return the value
    return forcePromise(promise);
}







SEXP rirEval_c(Code* c, SEXP env, unsigned numArgs) {
    SEXP call = constant(c->src);
    // make sure there is enough room on the stack
    checkStackSize(c->stackLength);
    iCheckStackSize(c->iStackLength);

    // get pc and bp regs, we do not need istack bp
    OpcodeT * pc = code(c);
    size_t bp = stack_.length;

    // main loop
    while (true) {
        switch (readOpcode(&pc)) {
        case push_: {
            SEXP x = readConst(&pc);
            push(x);
            break;
        }
        case ldfun_: {
            SEXP sym = readConst(&pc);
            SEXP val = findVar(sym, env);
            R_Visible = TRUE;

            // TODO something should happen here
            if (val == R_UnboundValue)
                assert(false && "Unbound var");
            else if (val == R_MissingArg)
                assert(false && "Missing argument");

            // if promise, evaluate & return
            if (TYPEOF(val) == PROMSXP)
                val = promiseValue(val);

            // WTF? is this just defensive programming or what?
            if (NAMED(val) == 0 && val != R_NilValue)
                SET_NAMED(val, 1);

            switch (TYPEOF(val)) {
            case CLOSXP:
                // TODO we do not need lazy compiling anymore
                /*
                val = (SEXP)jit(val);
                */
                break;
            case SPECIALSXP:
            case BUILTINSXP: {
                // TODO fix this
                /**
                SEXP prim = Primitives::compilePrimitive(val);
                if (prim)
                    val = prim;
                **/
                break;
            }
            default:
                // TODO!
                assert(false);
            }
            push(val);
            break;
        }
        case ldvar_: {
            SEXP sym = readConst(&pc);
            SEXP val = findVar(sym, env);
            R_Visible = TRUE;

            // TODO better errors
            if (val == R_UnboundValue)
                assert(false && "Unbound var");
            else if (val == R_MissingArg)
                assert(false && "Missing argument");

            // if promise, evaluate & return
            if (TYPEOF(val) == PROMSXP)
                val = promiseValue(val);

            // WTF? is this just defensive programming or what?
            if (NAMED(val) == 0 && val != R_NilValue)
                SET_NAMED(val, 1);

            push(val);
            break;
        }
        case call_: {
            // one huge large big **** T O D O ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! !
            break;
        }
        case promise_: {
            unsigned codeOffset = readImmediate(&pc);
            Code * promiseCode = codeAt(function(c), codeOffset);
            // TODO make the promise with current environment and the code object
            // and push it to the stack
            break;
        }
        case close_: {
            SEXP body = pop();
            SEXP arglist = pop();
            // TODO create cloisure (R's ) from the arglist and body and env and push it
            break;
        }
        case ret_: {
            return pop();
        }
        case force_: {
            SEXP p = pop();
            assert(TYPEOF(p) == PROMSXP);
            // TODO what if it is forced?
            push(forcePromise(p));
            break;
        }
        case pop_: {
            pop();
            break;
        }
        case pusharg_: {
            unsigned n = readImmediate(&pc);
            assert(n < numArgs);
            push(at(bp - numArgs + n));
            break;
        }
        case asast_:
        case stvar_:
        case asbool_:
        case condtrue_:
        case condfals_:
        case jmp_:
        case lti_:
        case eqi_:
        case pushi_:
        case dupi_:
        case dup_:
        case add_:
        case sub_:
        case lt_:
        case isspecial_:
        case isfun_:
        default:
            assert(false && "wrong or unimplemented opcode");
        }
    }




}





#ifdef HAHA

#include <assert.h>

#include "interpreter.h"

#define bool int
#define true 1
#define false 0

#define INLINE __attribute__((always_inline)) inline

// Stuff from R
extern SEXP R_TrueValue;
extern SEXP R_FalseValue;
extern SEXP Rf_NewEnvironment(SEXP, SEXP, SEXP);
extern Rboolean R_Visible;
extern SEXP forcePromise(SEXP);



Code * begin(Function * f) {
    return (Code *)(f + 1);
}

Code * end(Function * f) {
    return (Code *)((char *)f + f->size);
}

Function * function(Code * c) {
    return (Function *)((char *)c - c->offset);
}

BC_t * code(Code * c) {
    return (BC_t *)(c + 1);
}

unsigned * debugInfo(Code *c) {
    return (unsigned *)(c + 1) + sizeInInts(c->size);
}

Code * next(Code * c) {
   return (Code *)((char*)(c + 1) + sizeInInts(c->size) * 4 + c->numInsns);
}

unsigned sizeInInts(unsigned sizeInBytes) {
    unsigned x = sizeInBytes % 4;
    if (x != 0)
        sizeInBytes += (4 - x);
    return sizeInBytes / 4;
}


SEXP getCodeAST(Code * c) {
    return getAST(c->src);
}

SEXP getAST(unsigned index) {
    return source(index);
}


// -----------------------------------------------------------------------------
// stacks
// -----------------------------------------------------------------------------

#define STACK_TEMPLATE(CLS, T) \
typedef struct CLS { \
    T * data; \
    size_t size; \
    size_t capacity; \
} CLS;\
\
CLS * CLS ## _create(size_t n) { \
    CLS * result = malloc(sizeof(CLS)); \
    result->capacity = n; \
    result->size = 0; \
    result->data = malloc(n * sizeof(T)); \
    return result; \
} \
\
void CLS ## _grow(CLS * stack) { \
    size_t newCap = stack->capacity* 2;\
    T * bigger = malloc(newCap * sizeof(T)); \
    memcpy(bigger, stack->data, stack->capacity); \
    stack->capacity = newCap; \
    free(stack->data); \
    stack->data = bigger; \
} \
\
void CLS ## _push(CLS * stack, T s) { \
    if (stack->size == stack->capacity) \
        CLS ## _grow(stack); \
    stack->data[stack->size++] = s; \
} \
\
bool CLS ## _empty(CLS * stack) { \
    return stack->size == 0; \
} \
\
T CLS ## _pop(CLS * stack) { \
    return stack->data[--stack->size]; \
} \
\
void CLS ## _popn(CLS * stack, size_t n) { \
    stack->size -= n; \
} \
\
T CLS ## _peek(CLS * stack, size_t offset) { \
    return stack->data[stack->size - 1 - offset]; \
} \
\
T CLS ## _set(CLS * stack, size_t offset, T val) { \
    return stack->data[stack->size - 1 - offset] = val; \
} \
\
T CLS ## _at(CLS * stack, size_t pos) { \
    return stack->data[pos]; \
} \
\
T CLS ## _top(CLS * stack) { \
    return stack->data[stack->size -1]; \
}

STACK_TEMPLATE(Stack, SEXP)
STACK_TEMPLATE(StackI, int)

Stack stack;
StackI stacki;

// -----------------------------------------------------------------------------
// Bytecode access
// -----------------------------------------------------------------------------

INLINE BC readBC(BC_t ** pc) {
    BC result = **pc;
    *pc += 1;
    return result;
}

INLINE int readJumpTarget(BC_t ** pc) {
    unsigned result = *(JumpTarget*)*pc;
    *pc += sizeof(JumpTarget);
    return result;
}

INLINE unsigned readArgInex(BC_t **  pc) {
    unsigned result = *(ArgCount*)*pc;
    *pc += sizeof(ArgCount);
    return result;
}




// -----------------------------------------------------------------------------
// Interpreter op  & support
// -----------------------------------------------------------------------------

INLINE SEXP getCurrentCall(Code * cur, BC_t * pc, SEXP call) {
    // TODO find proper ast and return it
    return call;
}

INLINE SEXP getArg(unsigned idx, unsigned numArgs, size_t bp) {
    return Stack_at(&stack, bp - numArgs + idx);
}

INLINE SEXP loadConst(BC_t ** pc) {
    // TODO load the constant
    return NULL;
}





SEXP rirEval_c(Code * cur, SEXP env, unsigned numArgs) {
    SEXP call = getCodeAST(cur);
    // cannot be register because of the read functions taking address of it
    BC_t * pc = code(cur);
    size_t bp = stack.size;



    while (true) {
        switch (readBC(&pc)) {
        case to_bool: {
            SEXP t = Stack_top(&stack);
            int cond = NA_LOGICAL;
            if (Rf_length(t) > 1)
                warningcall(getCurrentCall(cur, pc, call), "the condition has length > 1 and only the first element will be used");
            Stack_pop(&stack);
            if (Rf_length(t) > 0) {
                /* inline common cases for efficiency */
                switch (TYPEOF(t)) {
                case LGLSXP:
                    cond = LOGICAL(t)[0];
                    break;
                case INTSXP:
                    cond =
                        INTEGER(t)[0]; /* relies on NA_INTEGER == NA_LOGICAL */
                    break;
                default:
                    cond = asLogical(t);
                }
            }
            if (cond == NA_LOGICAL) {
                const char* msg =
                    Rf_length(t)
                        ? (isLogical(t)
                               ? ("missing value where TRUE/FALSE needed")
                               : ("argument is not interpretable as logical"))
                        : ("argument is of length zero");
                errorcall(getCurrentCall(cur, pc, call), msg);
            }
            if (cond)
                Stack_push(&stack, R_TrueValue);
            else
                Stack_push(&stack, R_FalseValue);
            break;
        }
        case jmp: {
            pc = pc + readJumpTarget(&pc);
            break;
        }
        case jmp_true: {
            JumpTarget j = readJumpTarget(&pc);
            if (Stack_pop(&stack) == R_TrueValue)
                pc = pc + j;
            break;
        }
        case jmp_false: {
            JumpTarget j = readJumpTarget(&pc);
            if (Stack_pop(&stack) == R_FalseValue)
                pc = pc + j;
            break;
        }
        case push: {
            SEXP c = loadConst(&pc);
            Stack_push(&stack, c);
            break;
        }

        case lti: {
            int b = StackI_pop(&stacki);
            int a = StackI_pop(&stacki);
            Stack_push(&stack, a < b ? R_TrueValue : R_FalseValue);
            break;
        }

        case eqi: {
            int b = StackI_pop(&stacki);
            int a = StackI_pop(&stacki);
            Stack_push(&stack, a == b ? R_TrueValue : R_FalseValue);
            break;
        }

        case check_special: {
            SEXP sym = loadConst(&pc);
            SEXP val = findVar(sym, env);
            assert(TYPEOF(val) == SPECIALSXP);
            break;
        }

        case check_function: {
            SEXP f = Stack_top(&stack);
            assert(TYPEOF(f) == CLOSXP || TYPEOF(f) == BUILTINSXP ||
                   TYPEOF(f) == SPECIALSXP);
        }
        case getfun: {
            SEXP sym = loadConst(&pc);
            SEXP val = findVar(sym, env);
            R_Visible = TRUE;

            if (val == R_UnboundValue)
                assert(false && "Unbound var");
            else if (val == R_MissingArg)
                assert(false && "Missing argument");

            if (TYPEOF(val) == PROMSXP) {
                assert(false && "IMPLEMENT ME!!");
                /*
                BCProm* prom = getBCProm(val);

                if (prom->val(val)) {
                    val = prom->val(val);
                } else {
                    val = forcePromise(prom, val);
                }
                */
            }

            // WTF? is this just defensive programming or what?
            if (NAMED(val) == 0 && val != R_NilValue)
                SET_NAMED(val, 1);

            switch (TYPEOF(val)) {
            case INTSXP:
                // TODO check that it is proper stuff
                break;
            case CLOSXP:
                // TODO jitting all functions on demand, probably not what we want
                //val = (SEXP)jit(val);
                break;
            case SPECIALSXP:
            case BUILTINSXP: {
                /*SEXP prim = Primitives::compilePrimitive(val);
                if (prim)
                    val = prim;
                break; */
            }

            default:
                // TODO!
                assert(false);
            }
            Stack_push(&stack, val);
            break;
        }
        case getvar: {
            SEXP sym = loadConst(&pc);
            SEXP val = findVar(sym, env);
            R_Visible = TRUE;

            if (val == R_UnboundValue)
                assert(false && "Unbound var");
            else if (val == R_MissingArg)
                assert(false && "Missing argument");

            if (TYPEOF(val) == PROMSXP) {
/*                BCProm* prom = getBCProm(val);

                if (prom->val(val)) {
                    val = prom->val(val);
                } else {
                    val = forcePromise(prom, val);
                } */
            }

            // WTF? is this just defensive programming or what?
            if (NAMED(val) == 0 && val != R_NilValue)
                SET_NAMED(val, 1);
            Stack_push(&stack, val);
            break;
        }
        case mkprom: {
            assert(false);
            /*fun_idx_t idx = readFunctionIndex(&pc);
            SEXP prom = mkBCProm(cur->children[idx], env);
            assert(cur->children.size() > idx);
            Stack_push(&stack, prom); */
            break;
        }

        case BC_call: {
            assert(false);
        }

        case load_arg: {
            unsigned a = readArgIndex(&pc);
            assert(a < numArgs);
            Stack_push(&stack, getArg(a, numArgs, bp));
            break;
        }

        case numargi: {
            StackI_push(&stacki, numArgs);
            break;
        }

        case get_ast: {
            SEXP t = Stack_pop(&stack);
            /*assert(isBCProm(t));
            BCProm* p = getBCProm(t);
            stack.push(p->ast()); */
            assert(false);
            break;
        }

        case setvar: {
            SEXP val = Stack_pop(&stack);
            SEXP sym = Stack_pop(&stack);
            // TODO: complex assign
            assert(TYPEOF(sym) == SYMSXP);
            defineVar(sym, val, env);
            Stack_push(&stack, val);
            break;
        }

        case force_all: {
            assert(false);
/*            for (size_t a = 0; a < numArgs; ++a) {
                SEXP arg = getArg(a);
                if (isBCProm(arg)) {
                    BCProm* prom = getBCProm(arg);
                    SEXP val = forcePromise(prom, arg);
                    stack.push(val);
                } else {
                    stack.push(arg);
                }
            } */
            break;
        }

        case force: {
            assert(false);
/*            SEXP tos = stack.pop();
            assert(isBCProm(tos));
            BCProm* prom = getBCProm(tos);
            SEXP val = forcePromise(prom, tos);
            stack.push(val); */
            break;
        }

        case pop:
            Stak_pop(&stack);
            break;

        case ret: {
            return Stack_pop(&pc);
        }
/*
        case BC_t::pushi: {
            stacki.push(BC::readImmediate<int>(&pc));
            break;
        }

        case BC_t::dup:
            stack.push(stack.top());
            break;

        case BC_t::dupi:
            stacki.push(stacki.top());
            break;

        case BC_t::load_argi: {
            int pos = stacki.pop();
            stack.push(getArg(pos));
            break;
        }

        case BC_t::inci: {
            stacki.top()++;
            break;
        }

        case BC_t::mkclosure: {
            SEXP body = stack.pop();
            SEXP arglist = stack.pop();

            SEXP bcls = jit(body, arglist, env);
            stack.push(bcls);
            break;
        }

        case BC_t::add: {
            SEXP rhs = stack.pop();
            SEXP lhs = stack.pop();
            if (Rinternals::typeof(lhs) == REALSXP &&
                Rinternals::typeof(lhs) == REALSXP && Rf_length(lhs) == 1 &&
                Rf_length(rhs) == 1) {
                SEXP res = Rf_allocVector(REALSXP, 1);
                SET_NAMED(res, 1);
                REAL(res)[0] = REAL(lhs)[0] + REAL(rhs)[0];
                stack.push(res);
            } else {
                static SEXP op = getPrimitive("+");
                static CCODE primfun = getPrimfun("+");
                SEXP res = callPrimitive(primfun, getCurrentCall(), op, env,
                                         {lhs, rhs});
                stack.push(res);
            }
            break;
        }

        case BC_t::sub: {
            SEXP rhs = stack.pop();
            SEXP lhs = stack.pop();
            if (Rinternals::typeof(lhs) == REALSXP &&
                Rinternals::typeof(lhs) == REALSXP && Rf_length(lhs) == 1 &&
                Rf_length(rhs) == 1) {
                SEXP res = Rf_allocVector(REALSXP, 1);
                SET_NAMED(res, 1);
                REAL(res)[0] = REAL(lhs)[0] - REAL(rhs)[0];
                stack.push(res);
            } else {
                static SEXP op = getPrimitive("-");
                static CCODE primfun = getPrimfun("-");
                SEXP res = callPrimitive(primfun, getCurrentCall(), op, env,
                                      git    {lhs, rhs});
                stack.push(res);
            }
            break;
        }

        case BC_t::lt: {
            SEXP rhs = stack.pop();
            SEXP lhs = stack.pop();
            if (Rinternals::typeof(lhs) == REALSXP &&
                Rinternals::typeof(lhs) == REALSXP && Rf_length(lhs) == 1 &&
                Rf_length(rhs) == 1) {
                stack.push(REAL(lhs)[0] < REAL(rhs)[0] ? R_TrueValue
                                                       : R_FalseValue);
            } else {
                static SEXP op = getPrimitive("<");
                static CCODE primfun = getPrimfun("<");
                SEXP res = callPrimitive(primfun, getCurrentCall(), op, env,
                                         {lhs, rhs});
                stack.push(res);
            }
            break;
        } */

        case num_of:
        case invalid:
            assert(false);
        default:
            assert(false && "Not implemented (yet)");
        }








    }
    return NULL;
}

/*
types for first class macros
ryan,
dave forman: ?verman
paul stens...: Typing for macros, or alpha relation for macros
scala and rust: process ASTs , are macros-ish


*/







#endif
