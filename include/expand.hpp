#ifndef __FN_EXPAND_HPP
#define __FN_EXPAND_HPP

#include "array.hpp"
#include "base.hpp"
#include "istate.hpp"
#include "llir.hpp"
#include "namespace.hpp"
#include "parse.hpp"

namespace fn {

using namespace fn_parse;

struct interpreter;

struct expander_meta {
    // largest value dollar symbol encountered. -1 if none is encountered.
    i16 max_dollar_sym = -1;
};

// a function tree is an intermediate structure used by the compiler and
// expander to hold information which will be discarded by the time the function
// is finished.
struct function_tree {
    // the vm_thread holding our unfinished function. This is also used for
    // macroexpansion, building values, and signaling errors.
    istate* S;
    // the function stub we're building
    function_stub* stub;
    // used to cache constants to prevent duplicate entries in the table
    table<value, constant_id> const_lookup;
    // functions contained in this one. This mirrors the structure of the
    // sub_funs array in the stub, i.e. if there are three entries here, there
    // are three entries there, and the corresponding function stub pointers
    // are equal.
    dyn_array<function_tree*> sub_funs;
    // parameters to the function in the order they appear on the stack
    dyn_array<symbol_id> params;
    // the form to be compiled. If set to nullptr, indicates an expansion error
    // occurred.
    llir_form* body;
};

// some handy routines to add constants to the current stub
constant_id add_const(istate* S, function_tree* ft, value v);
constant_id add_number_const(istate* S, function_tree* ft, f64 number);
constant_id add_string_const(istate* S, function_tree* ft, const string& str);
constant_id add_quoted_const(istate* S, function_tree* ft, ast_form* to_quote);
// add a subfunction to a function tree. This causes a new stub to be created
// too.
function_tree* add_sub_fun(istate* S, function_tree* ft);
// these are for writing code to the function tree. FIXME: should move these to
// the compiler.
constant_id writeu8(istate* S, function_tree* ft, u8 u);
constant_id writeu16(istate* S, function_tree* ft, u16 u);
void compile_const(istate* S, function_tree* ft, constant_id cid);

function_tree* init_function_tree(istate* S, function_stub* stub);
void free_function_tree(istate* S, function_tree* ft);

struct expander {
private:
    istate* S;
    function_tree* ft;

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

    llir_fn* expand_sub_fun(const source_loc& loc,
            ast_form* params,
            u32 body_length,
            ast_form** body,
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

    expander(istate* S, function_tree* ft)
        : S{S}
        , ft{ft} {
    }

    friend void expand(istate*, function_tree*, ast_form*);
};

void expand(istate* S, function_tree* ft, ast_form* form);

}

#endif
