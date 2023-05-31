#ifndef RECORDING_H
#define RECORDING_H

#include "api.h"
#include "compiler/pir/closure_version.h"
#include "compiler/pir/pir.h"
#include "runtime/Context.h"
#include "runtime/DispatchTable.h"
#include "runtime/TypeFeedback.h"
#include <R/r.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rir {
namespace recording {

class Replay;
class Record;

#define NO_INDEX ((size_t)-1)
#define GLOBAL_ENV_NAME ".GlobalEnv"

enum class SpeculativeContextType { Callees, Test, Values };

class CodeIndex {
    bool promise_;
    size_t index_;

  public:
    CodeIndex(bool promise, size_t index) : promise_(promise), index_(index) {}
    Code* locate(Function* fun);
};

struct SpeculativeContext {
    SpeculativeContextType type;

    union Value {
        std::array<size_t, rir::ObservedCallees::MaxTargets> callees;
        ObservedTest test;
        ObservedValues values;
    } value;

    SpeculativeContext(decltype(value.callees) callees)
        : type{SpeculativeContextType::Callees}, value{.callees = callees} {}

    SpeculativeContext(ObservedTest test)
        : type{SpeculativeContextType::Test}, value{.test = test} {}

    SpeculativeContext(ObservedValues values)
        : type{SpeculativeContextType::Values}, value{.values = values} {}
};

class Event {
  public:
    friend std::ostream& operator<<(std::ostream& out, const Event& e);
    virtual SEXP toSEXP() const = 0;
    virtual void fromSEXP(SEXP sexp) = 0;
    virtual void replay(Replay& replay, SEXP closure,
                        std::string& closure_name) const = 0;
};

class CompilationEvent : public Event {
  public:
    CompilationEvent(unsigned long dispatch_context,
                     std::vector<SpeculativeContext>&& speculative_contexts)
        : dispatch_context(dispatch_context),
          speculative_contexts(speculative_contexts) {}
    SEXP toSEXP() const override;
    void fromSEXP(SEXP sexp) override;
    void replay(Replay& replay, SEXP closure,
                std::string& closure_name) const override;

  private:
    unsigned long dispatch_context;

    std::vector<SpeculativeContext> speculative_contexts;
};

class DeoptEvent : public Event {
  public:
    DeoptEvent(size_t functionIdx, DeoptReason::Reason reason,
               int reasonCodeIdx, uint32_t reasonCodeOff, SEXP trigger);
    ~DeoptEvent();
    SEXP toSEXP() const override;
    void fromSEXP(SEXP file) override;
    void replay(Replay& replay, SEXP closure,
                std::string& closure_name) const override;

  private:
    size_t functionIdx_;
    DeoptReason::Reason reason_;
    /* negative indicates promise index, positive function index */
    int reasonCodeIdx_;
    uint32_t reasonCodeOff_;
    SEXP trigger_;
};

struct FunRecording {
    /* possibly empty name of the closure */
    std::string name;
    /* possibly empty name of the environment in which the name was bound to the
     * closure */
    std::string env;
    /* the CLOSXP serialized into RAWSXP using the R_SerializeValue */
    SEXP closure;

    std::vector<std::unique_ptr<Event>> events;
};

class Replay {
    SEXP recordings_;
    std::vector<SEXP> closures_;

    SEXP replayClosure(size_t idx);

  public:
    void replaySpeculativeContext(
        DispatchTable* dt,
        std::vector<SpeculativeContext>::const_iterator& ctxStart,
        std::vector<SpeculativeContext>::const_iterator& ctxEnd);

    void replaySpeculativeContext(
        Code* code, std::vector<SpeculativeContext>::const_iterator& ctxStart,
        std::vector<SpeculativeContext>::const_iterator& ctxEnd);

    Replay(SEXP recordings);

    ~Replay();

    size_t replay();
};

class Record {
    std::unordered_map<std::string, size_t> recordings_index_;
    std::vector<FunRecording> fun_recordings_;

  public:
    ~Record();

    std::pair<size_t, FunRecording&> initOrGetRecording(const SEXP cls,
                                                        std::string name = "");

    void recordSpeculativeContext(DispatchTable* dt,
                                  std::vector<SpeculativeContext>& ctx);

    void recordSpeculativeContext(const Code* code,
                                  std::vector<SpeculativeContext>& ctx);

    int findIndex(rir::Code* code, rir::Code* needle);
    SEXP save();
    void reset() {
        recordings_index_.clear();
        fun_recordings_.clear();
    }
};

// utilities
SEXP setClassName(SEXP s, const char* className);
std::string sexpAddress(const SEXP s);
bool stringStartsWith(const std::string& s, const std::string& prefix);
std::string getEnvironmentName(SEXP env);
SEXP getEnvironment(const std::string& name);

// C++ API
void recordCompile(const SEXP cls, const std::string& name,
                   const Context& assumptions);
void recordDeopt(const SEXP cls, DeoptReason reason, SEXP trigger);

} // namespace recording

} // namespace rir

// R API
REXPORT SEXP startRecordings();
REXPORT SEXP stopRecordings();
REXPORT SEXP resetRecordings();
REXPORT SEXP isRecordings();
REXPORT SEXP replayRecordings(SEXP recordings);
REXPORT SEXP replayRecordingsFromFile(SEXP filename);
REXPORT SEXP saveRecordings(SEXP filename);
REXPORT SEXP loadRecordings(SEXP filename);
REXPORT SEXP getRecordings();

#endif
