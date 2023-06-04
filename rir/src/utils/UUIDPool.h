//
// Created by Jakob Hain on 6/1/23.
//

#pragma once

#include "R/r.h"
#include "UUID.h"
#include "bc/BC_inc.h"
#include "interpreter/instance.h"

#include <unordered_map>

#define DO_INTERN

namespace rir {

/// A pool of SEXPs with a UUID.
/// When we deserialize some SEXPs, after deserialization we will check their
///    hash and try to reuse an SEXP already interned if possible. Otherwise we
///    store ("intern") for future deserializations.
class UUIDPool {
    static std::unordered_map<UUID, SEXP> interned;

    /// Intern the SEXP, except we already know its hash
    static SEXP intern(SEXP e, UUID uuid);
  public:
    /// Will hash the SEXP and then, if we've already interned, return the
    /// existing version. Otherwise we will insert it into the pool and return
    /// it as-is.
    static SEXP intern(SEXP e);
    /// Read item and intern
    static SEXP readItem(SEXP ref_table, R_inpstream_t in);
    /// Write item, ensuring that it will actually be reused in redundant
    /// readItem calls even on a separate process. Actually, this just calls
    /// WriteItem, but makes the readItem / writeItem calls more symmetric
    /// because readItem has to intern
    static void writeItem(SEXP sexp, SEXP ref_table, R_outpstream_t out);
};

} // namespace rir