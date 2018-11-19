#ifndef COMPILER_VISITOR_H
#define COMPILER_VISITOR_H

#include "../pir/bb.h"
#include "../pir/code.h"
#include "../pir/instruction.h"
#include "../pir/pir.h"
#include "../util/cfg.h"

#include <deque>
#include <functional>
#include <random>
#include <stack>
#include <unordered_set>

namespace rir {
namespace pir {

namespace VisitorHelpers {
typedef std::function<bool(BB*)> BBActionPredicate;
typedef std::function<void(BB*)> BBAction;

/*
 * PredicateWrapper abstracts over BBAction and BBActionPredicate, to be
 * able to use them in the same generic implementation.
 * In the case of BBAction the return value will always be true.
 *
 */
template <typename ActionKind>
struct PredicateWrapper {};

template <>
struct PredicateWrapper<BBAction> {
    const BBAction action;
    bool operator()(BB* bb) const {
        action(bb);
        return true;
    }
};

template <>
struct PredicateWrapper<BBActionPredicate> {
    const BBActionPredicate action;
    bool operator()(BB* bb) const { return action(bb); }
};

/*
 * Helpers to remember which BB has already been visited. There is a fast
 * version (IDMarker) that uses a bitvector, but relies on stable BB ids. And
 * there is a slow version (PointerMarker) using a set.
 *
 */
struct IDMarker {
    std::vector<bool> done;
    IDMarker() : done(128, false){};

    void set(BB* bb) {
        while (bb->id >= done.size())
            done.resize(done.size() * 2);
        done[bb->id] = true;
    }

    bool check(BB* bb) { return bb->id < done.size() && done[bb->id]; }
};

struct PointerMarker {
    std::unordered_set<BB*> done;
    void set(BB* bb) { done.insert(bb); }
    bool check(BB* bb) { return done.find(bb) != done.end(); }
};

/*
 * Support for reverse iteration in range-based for loops.
 */
template <typename T>
struct reverse_wrapper {
    T& iterable;
};

template <typename T>
auto begin(reverse_wrapper<T> w) {
    return w.iterable.rbegin();
}

template <typename T>
auto end(reverse_wrapper<T> w) {
    return w.iterable.rend();
}

template <typename T>
reverse_wrapper<T> reverse(T&& iterable) {
    return {iterable};
}

}; // namespace VisitorHelpers

enum class Order { Depth, Breadth, Random, Lowering };
enum class Direction { Forward, Backward };

template <Order ORDER, Direction DIR, class Marker>
class VisitorImplementation {
  public:
    typedef std::function<bool(Instruction*)> InstrActionPredicate;
    typedef std::function<void(Instruction*)> InstrAction;
    typedef std::function<bool(Instruction*, BB*)> InstrBBActionPredicate;
    typedef std::function<void(Instruction*, BB*)> InstrBBAction;
    using BBActionPredicate = VisitorHelpers::BBActionPredicate;
    using BBAction = VisitorHelpers::BBAction;

    /*
     * Instruction Visitors
     *
     */
    static void run(BB* bb, InstrAction action) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        run(bb, [action](BB* bb) {
            for (auto i : *bb)
                action(i);
        });
    }

    static void run(CFG const& cfg, BB* bb, InstrAction action) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        run(cfg, bb, [action](BB* bb) {
            for (auto i : VisitorHelpers::reverse(*bb))
                action(i);
        });
    }

    static bool check(BB* bb, InstrActionPredicate action) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        return check(bb, [action](BB* bb) {
            bool holds = true;
            for (auto i : *bb) {
                if (!action(i)) {
                    holds = false;
                    break;
                }
            }
            return holds;
        });
    }

    static bool check(CFG const& cfg, BB* bb, InstrActionPredicate action) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        return check(cfg, bb, [action](BB* bb) {
            bool holds = true;
            for (auto i : VisitorHelpers::reverse(*bb)) {
                if (!action(i)) {
                    holds = false;
                    break;
                }
            }
            return holds;
        });
    }

    static void run(BB* bb, InstrBBAction action) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        run(bb, [action](BB* bb) {
            for (auto i : *bb)
                action(i, bb);
        });
    }

    static void run(CFG const& cfg, BB* bb, InstrBBAction action) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        run(cfg, bb, [action](BB* bb) {
            for (auto i : VisitorHelpers::reverse(*bb))
                action(i, bb);
        });
    }

    static bool check(BB* bb, InstrBBActionPredicate action) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        return check(bb, [action](BB* bb) {
            bool holds = true;
            for (auto i : *bb) {
                if (!action(i, bb)) {
                    holds = false;
                    break;
                }
            }
            return holds;
        });
    }

    static bool check(CFG const& cfg, BB* bb, InstrBBActionPredicate action) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        return check(cfg, bb, [action](BB* bb) {
            bool holds = true;
            for (auto i : VisitorHelpers::reverse(*bb)) {
                if (!action(i, bb)) {
                    holds = false;
                    break;
                }
            }
            return holds;
        });
    }

    /*
     * BB Visitors
     *
     */
    static void run(BB* bb, BBAction action, bool processNewNodes = false) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        genericRun(bb, action, nullptr, processNewNodes);
    }

    static void run(CFG const& cfg, BB* bb, BBAction action,
                    bool processNewNodes = false) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        genericRun(bb, action, &cfg, processNewNodes);
    }

    static bool check(BB* bb, BBActionPredicate action) {
        static_assert(DIR == Direction::Forward,
                      "Backward visitors need a CFG.");
        return genericRun(bb, action, nullptr, false);
    }

    static bool check(CFG const& cfg, BB* bb, BBActionPredicate action) {
        static_assert(DIR == Direction::Backward,
                      "Only backward visitors can take a CFG.");
        return genericRun(bb, action, &cfg, false);
    }

  protected:
    template <typename ActionKind>
    static bool genericRun(BB* bb, ActionKind action, CFG const* cfg,
                           bool processNewNodes) {
        typedef VisitorHelpers::PredicateWrapper<ActionKind> PredicateWrapper;
        const PredicateWrapper predicate = {action};

        BB* cur = bb;
        std::deque<BB*> todo;
        std::deque<BB*> delayed;
        Marker done;
        BB* next = nullptr;
        done.set(cur);

        auto schedule = [&](BB* bb) {
            if (!bb || done.check(bb))
                return;
            bool deoptBranch =
                !bb->isEmpty() && ScheduledDeopt::Cast(bb->last());
            if (ORDER == Order::Lowering && deoptBranch) {
                delayed.push_back(bb);
            } else if (!next && todo.empty()) {
                next = bb;
            } else {
                enqueue(todo, bb);
            }
            done.set(bb);
        };

        auto scheduleNext = [&]() {
            if (DIR == Direction::Forward) {
                if (ORDER == Order::Lowering) {
                    // Curently we emit only brtrue in pir2rir, therefore we
                    // always want next1 to be the fallthrough case.
                    schedule(cur->next1);
                    schedule(cur->next0);
                } else {
                    schedule(cur->next0);
                    schedule(cur->next1);
                }
            } else {
                for (auto pred : cfg->immediatePredecessors(cur))
                    schedule(pred);
            }
        };

        while (cur) {
            next = nullptr;

            if (!processNewNodes)
                scheduleNext();
            if (!predicate(cur))
                return false;
            if (processNewNodes)
                scheduleNext();

            if (!next) {
                if (!todo.empty()) {
                    next = todo.front();
                    todo.pop_front();
                } else if (!delayed.empty()) {
                    next = delayed.front();
                    delayed.pop_front();
                }
            }

            cur = next;
        }
        assert(todo.empty());

        return true;
    }

  private:
    static bool coinFlip() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::bernoulli_distribution coin(0.5);
        return coin(gen);
    };

    static void enqueue(std::deque<BB*>& todo, BB* bb) {
        // For analysis random search is faster
        if (ORDER == Order::Breadth || ORDER == Order::Lowering ||
            (ORDER == Order::Random && coinFlip()))
            todo.push_back(bb);
        else
            todo.push_front(bb);
    }
};

class Visitor : public VisitorImplementation<Order::Random, Direction::Forward,
                                             VisitorHelpers::IDMarker> {};

class BreadthFirstVisitor
    : public VisitorImplementation<Order::Breadth, Direction::Forward,
                                   VisitorHelpers::IDMarker> {};

class DepthFirstVisitor
    : public VisitorImplementation<Order::Depth, Direction::Forward,
                                   VisitorHelpers::IDMarker> {};

class LoweringVisitor
    : public VisitorImplementation<Order::Lowering, Direction::Forward,
                                   VisitorHelpers::IDMarker> {};

class BackwardVisitor
    : public VisitorImplementation<Order::Random, Direction::Backward,
                                   VisitorHelpers::IDMarker> {};

class BreadthFirstBackwardVisitor
    : public VisitorImplementation<Order::Breadth, Direction::Backward,
                                   VisitorHelpers::IDMarker> {};

template <class Marker = VisitorHelpers::IDMarker>
class DominatorTreeVisitor {
    using BBAction = VisitorHelpers::BBAction;

    const DominanceGraph& dom;

  public:
    explicit DominatorTreeVisitor(const DominanceGraph& dom) : dom(dom) {}

    void run(Code* code, BBAction action) {
        Marker done;

        std::stack<BB*> todo;
        std::stack<BB*> delayedTodo;

        todo.push(code->entry);
        done.set(code->entry);

        BB* cur;
        while (!todo.empty() || !delayedTodo.empty()) {
            if (!todo.empty()) {
                cur = todo.top();
                todo.pop();
            } else {
                cur = delayedTodo.top();
                delayedTodo.pop();
            }

            auto apply = [&](BB* next) {
                if (!next || done.check(next))
                    return;
                if (dom.dominates(cur, next)) {
                    todo.push(next);
                } else {
                    delayedTodo.push(next);
                }
                done.set(next);
            };

            apply(cur->next0);
            apply(cur->next1);

            action(cur);
        }
    }
};

} // namespace pir
} // namespace rir

#endif
