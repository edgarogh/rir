#include "visualization.h"
#include "interpreter/instance.h"
#include "runtime/Code.h"
#include "runtime/DispatchTable.h"
#include <sstream>

namespace rir {
namespace visualization {

SEXP viz(SEXP varargs) {
    std::ofstream f("viz.dot", std::ios_base::out);
    f << "digraph {" << std::endl;

    Printer p(f);
    assert(TYPEOF(varargs) == VECSXP);
    for (int i = 0; i < Rf_length(varargs); i++) {
        p.print(VECTOR_ELT(varargs, i), true);
    }

    f << "}" << std::endl;
    f.flush();
    f.close();
    return R_TrueValue;
}

// For use inside GDB, mostly
extern "C" {
SEXP vizOne(SEXP sexp) {
    SEXP compound = PROTECT(Rf_allocVector(VECSXP, 1));
    SET_VECTOR_ELT(compound, 0, sexp);
    SEXP res = viz(compound);
    UNPROTECT(1);
    return res;
}

size_t vizv(SEXP roots, ...) {
    const size_t MAX_ARGS = 16;

    va_list args;
    size_t i = 0;
    SEXP compound = PROTECT(Rf_allocVector(VECSXP, MAX_ARGS));

    for (va_start(args, roots); roots; roots = va_arg(args, SEXP)) {
        assert(i < MAX_ARGS);
        SET_VECTOR_ELT(compound, i++, roots);
    }

    SETLENGTH(compound, i);
    viz(compound);
    UNPROTECT(1);
    return LENGTH(compound);
}
}

void Printer::make_node(const void* ptr, const std::string& name,
                        const char* shape) {
    f << ((size_t)ptr) << "[label=\"" << name << "\",shape=" << shape << "]";
}

#define PRINT_HEADER(arg)                                                      \
    std::stringstream name_ss;                                                 \
    name_ss << (size_t)(arg);                                                  \
    auto name = name_ss.str();                                                 \
    if (!seen.insert(arg).second)                                              \
        return name;

std::string Printer::print(SEXP sexp, bool isRoot) {
    PRINT_HEADER(sexp);

    f << name << "[label=\"" << sexp << "(" << Rf_type2char(TYPEOF(sexp))
      << ")\",shape=egg]" << '\n';

    if (isRoot) {
        f << name << "[fillcolor=\"#ffa0ff80\",style=filled]" << '\n';
    }

    switch (TYPEOF(sexp)) {
    case CLOSXP: {
        auto name_b = print(BODY(sexp));
        f << name << "->" << name_b
          << "[label=\"has for body\",arrowhead=odiamond]" << '\n';
        break;
    }
    case EXTERNALSXP: {
        if (auto code = rir::Code::check(sexp)) {
            auto code_name = print(code);
            f << name << "->" << code_name << "[label=\"contains R.R.O.\"]"
              << '\n';
        } else if (auto dt = rir::DispatchTable::check(sexp)) {
            auto dt_name = print(dt);
            f << name << "->" << dt_name << "[label=\"contains R.R.O.\"]"
              << '\n';
        } else if (auto fun = rir::Function::check(sexp)) {
            auto fun_name = print(fun);
            f << name << "->" << fun_name << "[label=\"contains R.R.O.\"]"
              << '\n';
        }
        break;
    }
    case VECSXP: {
        for (int i = 0; i < Rf_length(sexp); i++) {
            SEXP sub_sexp = VECTOR_ELT(sexp, i);
            auto sub_sexp_name = print(sub_sexp);
            f << name << "->" << sub_sexp_name << "[label=\"el#" << i << "\"]"
              << '\n';
        }
        break;
    }
    case CHARSXP: {
        for (int i = 0; i < Rf_length(sexp); i++) {
            SEXP sub_sexp = STRING_ELT(sexp, i);
            if (TYPEOF(sub_sexp) == STRSXP) {
                auto sub_sexp_name = printString(sub_sexp);
                f << name << "->" << sub_sexp_name << "[label=\"el#" << i
                  << "\"]" << '\n';
            }
        }
    }
    }

    return name;
}

std::string Printer::printString(SEXP str) {
    assert(Rf_isString(str));
    PRINT_HEADER(str);
    f << name << "[label=\"\\\"" << str << "\\\"\",shape=note]" << '\n';
    return name;
}

std::string Printer::print(DispatchTable* dt) {
    PRINT_HEADER(dt);

    f << name << "[shape=cds,label=\"dt " << dt << "\"]" << '\n';

    for (size_t i = 0; i < dt->size(); i++) {
        auto dt_fun = dt->get(i);
        auto dt_fun_name = print(dt_fun);
        const char* maybeBaseline = i == 0 ? " (baseline)" : "";
        f << name << "->" << dt_fun_name << "[label=\"dt#" << i << maybeBaseline
          << "\",arrowhead=odiamond]" << '\n';
    }

    return name;
}

std::string Printer::print(Code* code) {
    PRINT_HEADER(code);

    if (code->isCompiled()) {
        f << name << "[shape=box3d,label=\"native code " << code << "\"]"
          << '\n';
    } else {
        std::stringstream label_ss;
        label_ss << name << "[shape=record,label=\"{code " << code;

        Opcode* end = code->endCode();
        Opcode* pc = code->code();
        Opcode* prev = nullptr;
        Opcode* pprev = nullptr;

        size_t i = 0;
        while (pc < end) {
            label_ss << "|<e" << i << ">";
            auto bc = BC::decode(pc, code);

            switch (bc.bc) {
            case Opcode::record_call_: {
                auto& extra = bc.callFeedbackExtra();

                label_ss << "record_call_";

                size_t j = 0;
                for (SEXP callee : extra.targets) {
                    auto callee_name = print(callee);
                    f << name << ":e" << i << "->" << callee_name
                      << "[label=\"O.C #" << j << "\"]" << '\n';
                    j++;
                }
                break;
            }
            case Opcode::close_: {
                auto body = BC::decodeShallow(pprev).immediateConst();

                label_ss << "close_";
                auto body_name = print(body);

                f << name << ":e" << i << "->" << body_name << std::endl;

                break;
            }
            default: {
            }
            }
            pprev = prev;
            prev = pc;
            pc = BC::next(pc);
            i++;
        }

        label_ss << "}\"]";
        f << label_ss.str() << std::endl;
    }

    /* for (size_t i = 0; i < code->extraPoolSize; i++) {
        auto extra_sexp_name = print(code->getExtraPoolEntry(i));
        f << name << "->" << extra_sexp_name << "[label=\"references E.P.E #"
          << i << "\"]" << '\n';
    } */

    return name;
}

std::string Printer::print(Function* fun) {
    PRINT_HEADER(fun);

    f << name << "[shape=parallelogram,label=\"version " << fun << "\"]"
      << std::endl;
    auto body_name = print(fun->body());
    f << name << "->" << body_name << "[label=\"has code body\"]" << std::endl;

    return name;
}

} // namespace visualization
} // namespace rir
