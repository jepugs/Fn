#ifndef __FN_EXPAND_HPP
#define __FN_EXPAND_HPP

#include "base.hpp"
#include "llir.hpp"
#include "namespace.hpp"
#include "parse.hpp"

namespace fn {

using namespace fn_parse;

struct interpreter;

struct expander_meta {
    // largest value dollar symbol encountered. -1 if none is encountered.
    i16 max_dollar_sym;
};

class expander {
private:
    interpreter* inter;
    code_chunk* chunk;

    bool is_macro(symbol_id sym);

    // _m means including a metadata struct;

    // expands a list form as if it's a function call. Assumes lst.length_length
    // >= 1.
    llir_form* expand_call(ast_form* lst, expander_meta* meta);
    // assumes lst[0] is a symbol
    llir_form* expand_symbol_list(ast_form* lst, expander_meta* meta);
    // assumes lst has kind ak_list
    llir_form* expand_list(ast_form* lst, expander_meta* meta);
    // no assumptions on ast
    llir_form* expand(ast_form* ast, expander_meta* meta);

public:
    expander(interpreter* inter, code_chunk* const_chunk);
    llir_form* expand_ast(ast_form* form);
};

// Expand an ast_form fully into an llir_form in preparation for for
// compilation. The chunk here is needed to hold constants
llir_form* expand_ast(ast_form* form, interpreter* inter, code_chunk* chunk);
}

#endif
