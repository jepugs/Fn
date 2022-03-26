#ifndef __FN_SYMBOLS_HPP
#define __FN_SYMBOLS_HPP

#include "base.hpp"

namespace fn {


// standard symbol cache indices
enum sc_index {
    SC___CALL,
    SC_FN_BUILTIN,
    SC_FN_BUILTIN__FUNCTION,
    SC_FN_BUILTIN__LIST,
    SC_FN_BUILTIN__STRING,
    SC_FN_BUILTIN__TABLE,
    SC_FN_INTERNAL,
    SC_APPLY,
    SC_DEF,
    SC_FN,
    SC_LIST,
    SC_NAMESPACE,
    SC_NIL,
    SC_NO,
    SC_QUOTE,
    SC_STRING,
    SC_TABLE,
    SC_YES
};

// names for the symbol cache symbols
constexpr const char* sc_names[] = {
    "__call",
    "fn/builtin",
    "fn/builtin:Function",
    "fn/builtin:List",
    "fn/builtin:String",
    "fn/builtin:Table",
    "fn/internal",
    "apply",
    "def",
    "fn",
    "List",
    "namespace",
    "nil",
    "no",
    "quote",
    "String",
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
