#include "osr.h"

#include "compiler/backend.h"
#include "compiler/compiler.h"
#include "pir/deopt_context.h"
#include "pir/pir_impl.h"

namespace rir {
namespace pir {

Function* OSR::compile(SEXP closure, rir::Code* c,
                       const ContinuationContext& ctx) {
    Function* fun = nullptr;

    // compile to pir
    pir::Module* module = new pir::Module;

    pir::StreamLogger logger(DebugOptions::DefaultDebugOptions);
    logger.title("Compiling continuation");
    pir::Compiler cmp(module, logger);

    pir::Backend backend(module, logger, "continuation");

    cmp.compileContinuation(
        closure, &ctx,
        [&](Continuation* cnt) {
            cmp.optimizeModule();

            fun = backend.getOrCompile(cnt);
        },
        [&]() { std::cerr << "Continuation compilation failed\n"; });

    delete module;

    return fun;
}

Function* OSR::deoptlessDispatch(SEXP closure, rir::Code* c,
                                 const DeoptContext& ctx) {
    DeoptlessDispatchTable* dispatchTable = nullptr;
    if (c->extraPoolSize > 0) {
        dispatchTable = DeoptlessDispatchTable::check(
            c->getExtraPoolEntry(c->extraPoolSize - 1));
    }
    if (!dispatchTable) {
        dispatchTable = DeoptlessDispatchTable::create();
        c->addExtraPoolEntry(dispatchTable->container());
    }

    Function* fun = dispatchTable->dispatch(ctx);

    if (!fun && !dispatchTable->full()) {
        assert(ctx.asDeoptContext());
        fun = compile(closure, c, ctx);
        if (fun)
            dispatchTable->insert(ctx, fun);
    }
    return fun;
}

} // namespace pir
} // namespace rir
