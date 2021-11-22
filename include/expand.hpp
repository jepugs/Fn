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

    bool is_operator_list(const string& op_name, ast_form* form);

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
    llir_form* expand_defmacro(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_defn(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);

    bool is_do_inline(ast_form* ast);
    bool is_let(ast_form* ast);
    bool is_letfn(ast_form* ast);
    // lst includes the do symbol at 0
    void flatten_do_body(u32 length,
            ast_form** lst,
            vector<ast_form*>& buf,
            expander_meta* meta);
    // ast_body doesn't include the do symbol
    llir_form* expand_let_in_do(u32 length,
            ast_form** ast_body,
            expander_meta* meta);
    // ast_body doesn't include the do symbl
    llir_form* expand_letfn_in_do(u32 length,
            ast_form** ast_body,
            expander_meta* meta);
    // ast_body doesn't include the do symbl
    bool expand_do_recur(u32 length,
            ast_form** ast_body,
            vector<llir_form*>& buf,
            expander_meta* meta);
    llir_form* expand_do(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_do_inline(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);

    llir_form* expand_dollar_fn(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_dot(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);

    llir_form* expand_fn(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);

    llir_form* expand_if(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_import(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_let(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_letfn(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_or(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_quote(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_set(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_unquote(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_unquote_splicing(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_with(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);

    // expands a list form as if it's a function call. Assumes len >= 1.
    llir_form* expand_call(const source_loc& loc,
            u32 len,
            ast_form** lst,
            expander_meta* meta);
    // assumes lst[0] is a symbol
    llir_form* expand_symbol_list(ast_form* lst, expander_meta* meta);
    // assumes lst has kind ak_list
    llir_form* expand_list(ast_form* lst, expander_meta* meta);
    // no assumptions on ast
    llir_form* expand_meta(ast_form* ast, expander_meta* meta);

    symbol_id intern(const string& str);
    symbol_id gensym();
    bool is_keyword(symbol_id sym) const;

public:
    expander(interpreter* inter, code_chunk* const_chunk);
    // expand an ast_form into llir
    llir_form* expand(ast_form* form, expand_error* err);
    // Attempt to populate params to the parameter list represented by ast.
    // Returns false on error.
    bool expand_params(ast_form* ast,
            llir_fn_params* params,
            expander_meta* meta);
};

}

#endif
