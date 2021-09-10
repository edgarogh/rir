#ifndef RIR_2_PIR_COMPILER_H
#define RIR_2_PIR_COMPILER_H

#include "R/Preserve.h"
#include "log/stream_logger.h"
#include "pir/pir.h"
#include "utils/FormalArgs.h"

#include <list>
#include <stack>

namespace rir {
struct DispatchTable;

namespace pir {
struct DeoptContext;

class Compiler {
  public:
    static constexpr Context::Flags minimalContext =
        Context::Flags(Assumption::CorrectOrderOfArguments) |
        Assumption::NotTooManyArguments;
    static constexpr Context defaultContext =
        Context(Context::Flags(Assumption::CorrectOrderOfArguments) |
                    Assumption::NotTooManyArguments,
                0);

    Compiler(Module* module, StreamLogger& logger)
        : module(module), logger(logger){};

    typedef std::function<void()> Maybe;
    typedef std::function<void(ClosureVersion*)> MaybeCls;
    typedef std::function<void(Continuation*)> MaybeCnt;

    void compileClosure(SEXP, const std::string& name, const Context& ctx,
                        bool root, MaybeCls success, Maybe fail,
                        std::list<PirTypeFeedback*> outerFeedback);
    void compileFunction(rir::DispatchTable*, const std::string& name,
                         SEXP formals, SEXP srcRef, const Context& ctx,
                         MaybeCls success, Maybe fail,
                         std::list<PirTypeFeedback*> outerFeedback);
    void compileContinuation(SEXP closure, const DeoptContext& ctx,
                             MaybeCnt success, Maybe fail);

    void optimizeModule();

    bool seenC = false;

    void preserve(SEXP c) { preserve_(c); }

    Module* module;

  private:
    StreamLogger& logger;

    void compileClosure(Closure* closure, rir::Function* optFunction,
                        const Context& ctx, bool root, MaybeCls success,
                        Maybe fail, std::list<PirTypeFeedback*> outerFeedback);

    Preserve preserve_;
};

} // namespace pir
} // namespace rir

#endif
