#ifndef __FN_SYMBOLS_HPP
#define __FN_SYMBOLS_HPP

#include "base.hpp"

namespace fn {


// standard symbol cache indices
enum sc_index {
    SC___CALL,
    SC_FN_BUILTIN,
    SC_APPLY,
    SC_DEF,
    SC_FN,
    SC_LIST,
    SC_NIL,
    SC_NO,
    SC_QUOTE,
    SC_TABLE,
    SC_YES
};

// names for the symbol cache symbols
constexpr const char* sc_names[] = {
    "__call",
    "fn/builtin",
    "apply",
    "def",
    "fn",
    "List",
    "nil",
    "no",
    "quote",
    "Table",
    "yes"
};

constexpr u32 SYMCACHE_SIZE = sizeof(sc_names) / sizeof(sc_names[0]);

// used to store precomputed symbols
struct symbol_cache {
    symbol_id syms[SYMCACHE_SIZE];
};

}

#endif
