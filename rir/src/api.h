#ifndef API_H_
#define API_H_

#include "R/r.h"
#include "compiler/log/debug.h"
#include "runtime/Context.h"

#include <stdint.h>

#define REXPORT extern "C"

extern int R_ENABLE_JIT;

namespace rir {
class UUIDHasher;
} // namespace rir
class ByteBuffer;

REXPORT SEXP rirInvocationCount(SEXP what);
REXPORT SEXP pirCompileWrapper(SEXP closure, SEXP name, SEXP debugFlags,
                               SEXP debugStyle);
REXPORT SEXP rirCompile(SEXP what, SEXP env);
REXPORT SEXP pirTests();
REXPORT SEXP pirCheck(SEXP f, SEXP check, SEXP env);
REXPORT SEXP pirSetDebugFlags(SEXP debugFlags);
SEXP pirCompile(SEXP closure, const rir::Context& assumptions,
                const std::string& name, const rir::pir::DebugOptions& debug,
                std::string* closureVersionPirPrint = nullptr);
extern SEXP rirOptDefaultOpts(SEXP closure, const rir::Context&, SEXP name);
extern SEXP rirOptDefaultOptsDryrun(SEXP closure, const rir::Context&,
                                    SEXP name);
REXPORT SEXP rirSerialize(SEXP data, SEXP file);
REXPORT SEXP rirDeserialize(SEXP file);
/// Hash an SEXP (doesn't have to be RIR) into the hasher, by serializing it
/// but XORing the bits instead of collecting them.
__attribute__((unused)) void hash(SEXP sexp, rir::UUIDHasher& hasher);
/// Serialize a SEXP (doesn't have to be RIR) into the buffer
void serialize(SEXP sexp, ByteBuffer& buffer);
/// Deserialize an SEXP (doesn't have to be RIR) from the buffer
SEXP deserialize(ByteBuffer& sexpBuffer);

REXPORT SEXP rirSetUserContext(SEXP f, SEXP udc);
REXPORT SEXP rirCreateSimpleIntContext();

__attribute__((unused)) REXPORT SEXP tryToRunCompilerServer();

#endif // API_H_
