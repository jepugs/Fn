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

struct expander_meta {
    // largest value dollar symbol encountered. -1 if none is encountered.
    i16 max_dollar_sym;
};

class expander {
private:
    interpreter* inter;
    code_chunk* chunk;
    // set on illegal syntax/failed macro expansion
    fault* err;

    bool is_macro(symbol_id sym);

    bool is_operator_list(const string& op_name, ast_form* form);

    llir_form* expand_and(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_apply(const source_loc& loc,
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
            dyn_array<ast_form*>* buf,
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
            dyn_array<llir_form*>* buf,
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

    // Attempt to populate params to the parameter list represented by ast.
    // Returns false on error.
    bool expand_params(ast_form* ast,
            llir_fn_params* params,
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

    bool is_unquote(ast_form* ast);
    bool is_unquote_splicing(ast_form* ast);
    llir_form* quasiquote_expand_recur(ast_form* form, expander_meta* meta);
    // quasiquoting a list, in the worst case, requires us to concatenate a
    // series of lists. This function travels along the argument list starting
    // at [0] and collects the next argument to concat. *stopped_at is set to
    // the place where we stopped, so that the caller can resume looking for the
    // next concat argument.
    llir_form* quasiquote_next_conc_arg(const source_loc& loc,
            u32 length,
            ast_form** lst,
            u32* stopped_at,
            expander_meta* meta);
    // for just this function, the first (list) argument of the quasiquote is
    // passed in
    llir_form* expand_quasiquote_list(const source_loc& loc,
            u32 length,
            ast_form** lst,
            expander_meta* meta);
    llir_form* expand_quasiquote(const source_loc& loc,
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
    string symbol_name(symbol_id name);
    bool is_keyword(symbol_id sym) const;

    // set the fault using the "expand" subsystem
    void e_fault(const source_loc& loc, const string& msg);

public:
    expander(interpreter* inter, code_chunk* const_chunk);
    // expand an ast_form into llir
    llir_form* expand(ast_form* form, fault* err);

};

}

#endif
