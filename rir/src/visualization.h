#ifndef RIR_VISUALIZATION_H
#define RIR_VISUALIZATION_H

#include "R/Serialize.h"
#include "interpreter/instance.h"
#include "runtime/DispatchTable.h"
#include <fstream>
#include <set>

namespace rir {
namespace visualization {

class Printer {
    std::ofstream& f;
    std::set<const void*> seen;

  private:
    void make_node(const void* ptr, const std::string& name, const char* shape);
    void make_edge(const std::string& from, const std::string& to,
                   const std::string& label);

  public:
    explicit Printer(std::ofstream& f) : f(f), seen({}) {}
    std::string print(SEXP sexp, bool isRoot = false);
    std::string printString(SEXP str);
    std::string print(DispatchTable* dt);
    std::string print(Code* code);
    std::string print(Function* fun);
};

SEXP viz(SEXP sexp);

} // namespace visualization
} // namespace rir

#endif // RIR_VISUALIZATION_H
