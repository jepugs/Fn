#ifndef __FN_SYMBOLS_HPP
#define __FN_SYMBOLS_HPP

#include "base.hpp"

namespace fn {

constexpr u32 SYMCACHE_SIZE = 8;

// used to store precomputed symbols
struct symbol_cache {
    symbol_id syms[SYMCACHE_SIZE];
};

// standard symbol cache indices
enum sc_index {
    SC___CALL,
    SC_FN_BUILTIN,
    SC_APPLY,
    SC_DEF,
    SC_NIL,
    SC_NO,
    SC_QUOTE,
    SC_YES
};

// names for the symbol cache symbols
constexpr const char* sc_names[] = {
    "__call",
    "fn/builtin",
    "apply",
    "def",
    "nil",
    "no",
    "quote",
    "yes"
};

}

#endif
