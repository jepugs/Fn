#ifndef __FN_SYMBOLS_HPP
#define __FN_SYMBOLS_HPP

#include "base.hpp"

namespace fn {

// The symbol cache is a fixed set of symbols that are automatically interned
// when the istate is initialized so that they can be accessed in the future
// without performing an intern first. It's here to avoid calling intern inside
// any tight loops, but it's not used very much.

// symbol cache indices
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
    SC_DEFMACRO,
    SC_DO,
    SC_DO_INLINE,
    SC_IF,
    SC_IMPORT,
    SC_FN,
    SC_LET,
    SC_QUOTE,
    SC_SET,
    SC_LIST,
    SC_NAMESPACE,
    SC_NIL,
    SC_NO,
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
    "defmacro",
    "do",
    "do-inline",
    "if",
    "import",
    "fn",
    "let",
    "quote",
    "set!",
    "List",
    "namespace",
    "nil",
    "no",
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
