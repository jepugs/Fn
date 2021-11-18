#ifndef __FN_EXPAND_HPP
#define __FN_EXPAND_HPP

#include "base.hpp"
#include "llir.hpp"
#include "llir.hpp"
#include "namespace.hpp"
#include "parse.hpp"

namespace fn {

using namespace fn_parse;

struct interpreter;

struct expand_error {
    source_loc origin;
    string message;
};

struct expander_meta {
    // largest value dollar symbol encountered. -1 if none is encountered.
    i16 max_dollar_sym;
    // In the event of an error, the various expand_ methods return null and set
    // this string. (It must be freed later).
    expand_error err;
};

class expander {
private:
    interpreter* inter;
    code_chunk* chunk;

    bool is_macro(symbol_id sym);

    llir_form* expand_and(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_cond(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_def(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    // expands a list form as if it's a function call. Assumes lst.length_length
    // >= 1.
    llir_form* expand_call(ast_form* lst, expander_meta* meta);
    // assumes lst[0] is a symbol
    llir_form* expand_symbol_list(ast_form* lst, expander_meta* meta);
    // assumes lst has kind ak_list
    llir_form* expand_list(ast_form* lst, expander_meta* meta);
    // no assumptions on ast
    llir_form* expand_meta(ast_form* ast, expander_meta* meta);

    symbol_id intern(const string& str);
    symbol_id gensym();

public:
    expander(interpreter* inter, code_chunk* const_chunk);
    llir_form* expand(ast_form* form, expand_error* err);
};

}

#endif
